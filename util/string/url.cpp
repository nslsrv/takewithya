#include "url.h"
#include "cast.h"
#include "util.h"
#include "cstriter.h"
#include "ascii.h"

#include <util/charset/unidata.h> // for ToLower
#include <util/system/maxlen.h>
#include <util/system/defaults.h>
#include <util/memory/tempbuf.h>
#include <util/generic/chartraits.h>
#include <util/generic/algorithm.h>
#include <util/generic/hash_set.h>
#include <util/generic/ptr.h>
#include <util/generic/yexception.h>
#include <util/generic/singleton.h>

#include <cstdlib>

namespace {
    struct TUncheckedSize {
        bool Has(size_t) const {
            return true;
        }
    };

    struct TKnownSize {
        size_t MySize;
        TKnownSize(size_t sz)
            : MySize(sz)
        {
        }
        bool Has(size_t sz) const {
            return sz <= MySize;
        }
    };

    template <typename TChar1, typename TChar2>
    int Compare1Case2(const TChar1* s1, const TChar2* s2, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            if (TCharTraits<TChar1>::ToLower(s1[i]) != s2[i])
                return TCharTraits<TChar1>::ToLower(s1[i]) < s2[i] ? -1 : 1;
        }
        return 0;
    }

    template <typename TChar, typename TTraits, typename TBounds>
    inline size_t GetHttpPrefixSizeImpl(const TChar* url, const TBounds& urlSize, bool ignorehttps) {
        const TChar httpPrefix[] = {'h', 't', 't', 'p', ':', '/', '/', 0};
        const TChar httpsPrefix[] = {'h', 't', 't', 'p', 's', ':', '/', '/', 0};
        if (urlSize.Has(7) && Compare1Case2(url, httpPrefix, 7) == 0)
            return 7;
        if (!ignorehttps && urlSize.Has(8) && Compare1Case2(url, httpsPrefix, 8) == 0)
            return 8;
        return 0;
    }

    template <typename T>
    inline T CutHttpPrefixImpl(const T& url, bool ignorehttps) {
        using TChar = typename T::char_type;
        using TTraits = typename T::traits_type;
        size_t prefixSize = GetHttpPrefixSizeImpl<TChar, TTraits>(url.data(), TKnownSize(url.size()), ignorehttps);
        if (prefixSize)
            return url.substr(prefixSize);
        return url;
    }
}

size_t GetHttpPrefixSize(const char* url, bool ignorehttps) {
    return GetHttpPrefixSizeImpl<char, TCharTraits<char>>(url, TUncheckedSize(), ignorehttps);
}

size_t GetHttpPrefixSize(const wchar16* url, bool ignorehttps) {
    return GetHttpPrefixSizeImpl<wchar16, TCharTraits<wchar16>>(url, TUncheckedSize(), ignorehttps);
}

size_t GetHttpPrefixSize(const TStringBuf url, bool ignorehttps) {
    return GetHttpPrefixSizeImpl<char, TCharTraits<char>>(url.data(), TKnownSize(url.size()), ignorehttps);
}

size_t GetHttpPrefixSize(const TWtringBuf url, bool ignorehttps) {
    return GetHttpPrefixSizeImpl<wchar16, TCharTraits<wchar16>>(url.data(), TKnownSize(url.size()), ignorehttps);
}

TStringBuf CutHttpPrefix(const TStringBuf url, bool ignorehttps) {
    return CutHttpPrefixImpl(url, ignorehttps);
}

TWtringBuf CutHttpPrefix(const TWtringBuf url, bool ignorehttps) {
    return CutHttpPrefixImpl(url, ignorehttps);
}

size_t GetSchemePrefixSize(const TStringBuf url) {
    struct TDelim: public str_spn {
        inline TDelim()
            : str_spn("!-/:-@[-`{|}", true)
        {
        }
    };

    const auto& delim = *Singleton<TDelim>();
    const char* n = delim.brk(~url, url.end());

    if (n + 2 >= url.end() || *n != ':' || n[1] != '/' || n[2] != '/') {
        return 0;
    }

    return n + 3 - url.begin();
}

TStringBuf GetSchemePrefix(const TStringBuf url) {
    return url.Head(GetSchemePrefixSize(url));
}

TStringBuf CutSchemePrefix(const TStringBuf url) {
    return url.Tail(GetSchemePrefixSize(url));
}

template <bool KeepPort>
static inline TStringBuf GetHostAndPortImpl(const TStringBuf url) {
    TStringBuf urlNoScheme = url;

    urlNoScheme.Skip(GetHttpPrefixSize(url));

    struct TDelim: public str_spn {
        inline TDelim()
            : str_spn(KeepPort ? "/;?#" : "/:;?#")
        {
        }
    };

    const auto& nonHostCharacters = *Singleton<TDelim>();
    const char* firstNonHostCharacter = nonHostCharacters.brk(urlNoScheme.begin(), urlNoScheme.end());

    if (firstNonHostCharacter != urlNoScheme.end()) {
        return urlNoScheme.substr(0, firstNonHostCharacter - ~urlNoScheme);
    }

    return urlNoScheme;
}

TStringBuf GetHost(const TStringBuf url) {
    return GetHostAndPortImpl<false>(url);
}

TStringBuf GetHostAndPort(const TStringBuf url) {
    return GetHostAndPortImpl<true>(url);
}

TStringBuf GetSchemeHostAndPort(const TStringBuf url, bool trimHttp, bool trimDefaultPort) {
    const size_t schemeSize = GetSchemePrefixSize(url);
    const TStringBuf scheme = url.Head(schemeSize);

    const bool isHttp = (schemeSize == 0 || scheme == STRINGBUF("http://"));

    TStringBuf hostAndPort = GetHostAndPort(url.Tail(schemeSize));

    if (trimDefaultPort) {
        const size_t pos = hostAndPort.find(':');
        if (pos != TStringBuf::npos) {
            const bool isHttps = (scheme == STRINGBUF("https://"));

            const TStringBuf port = hostAndPort.Tail(pos + 1);
            if ((isHttp && port == STRINGBUF("80")) || (isHttps && port == STRINGBUF("443"))) {
                // trimming default port
                hostAndPort = hostAndPort.Head(pos);
            }
        }
    }

    if (isHttp && trimHttp) {
        return hostAndPort;
    } else {
        return TStringBuf(scheme.begin(), hostAndPort.end());
    }
}

TStringBuf GetOnlyHost(const TStringBuf url) {
    return GetHost(CutSchemePrefix(url));
}

TStringBuf GetPathAndQuery(const TStringBuf url, bool trimFragment) {
    const size_t off = url.find('/', GetHttpPrefixSize(url));
    TStringBuf hostUnused, path;
    if (!url.TrySplitAt(off, hostUnused, path))
        return "/";

    return trimFragment ? path.Before('#') : path;
}

// this strange creature returns 2nd level domain, possibly with port
TStringBuf GetDomain(const TStringBuf host) {
    const char* c = !host ? ~host : host.end() - 1;
    for (bool wasPoint = false; c != ~host; --c) {
        if (*c == '.') {
            if (wasPoint) {
                ++c;
                break;
            }
            wasPoint = true;
        }
    }
    return TStringBuf(c, host.end());
}

TStringBuf GetParentDomain(const TStringBuf host, size_t level) {
    size_t pos = host.size();
    for (size_t i = 0; i < level; ++i) {
        pos = host.rfind('.', pos);
        if (pos == TString::npos)
            return host;
    }
    return host.SubStr(pos + 1);
}

TStringBuf GetZone(const TStringBuf host) {
    return GetParentDomain(host, 1);
}

TStringBuf CutWWWPrefix(const TStringBuf url) {
    if (+url >= 4 && url[3] == '.' && !strnicmp(~url, "www", 3))
        return url.substr(4);
    return url;
}

static inline bool IsSchemeChar(char c) noexcept {
    return IsAsciiAlnum(c); //what about '+' ?..
}

static bool HasPrefix(const TString& url) noexcept {
    TStringBuf scheme, unused;
    if (!TStringBuf(url).TrySplit("://", scheme, unused))
        return false;

    return AllOf(scheme, IsSchemeChar);
}

TString AddSchemePrefix(const TString& url) {
    return AddSchemePrefix(url, "http");
}

TString AddSchemePrefix(const TString& url, TString scheme) {
    if (HasPrefix(url)) {
        return url;
    }

    scheme.append("://");
    scheme.append(url);

    return scheme;
}

#define X(c) (c >= 'A' ? ((c & 0xdf) - 'A') + 10 : (c - '0'))

static inline int x2c(unsigned char* x) {
    if (!IsAsciiHex(x[0]) || !IsAsciiHex(x[1]))
        return -1;
    return X(x[0]) * 16 + X(x[1]);
}

#undef X

static inline int Unescape(char* str) {
    char *to, *from;
    int dlen = 0;
    if ((str = strchr(str, '%')) == nullptr)
        return dlen;
    for (to = str, from = str; *from; from++, to++) {
        if ((*to = *from) == '%') {
            int c = x2c((unsigned char*)from + 1);
            *to = char((c > 0) ? c : '0');
            from += 2;
            dlen += 2;
        }
    }
    *to = 0; /* terminate it at the new length */
    return dlen;
}

size_t NormalizeUrlName(char* dest, const TStringBuf source, size_t dest_size) {
    if (source.Empty() || source[0] == '?')
        return strlcpy(dest, "/", dest_size);
    size_t len = Min(dest_size - 1, source.length());
    memcpy(dest, source.data(), len);
    dest[len] = 0;
    len -= Unescape(dest);
    strlwr(dest);
    return len;
}

size_t NormalizeHostName(char* dest, const TStringBuf source, size_t dest_size, ui16 defport) {
    size_t len = Min(dest_size - 1, source.length());
    memcpy(dest, source.data(), len);
    dest[len] = 0;
    char buf[8] = ":";
    size_t buflen = 1 + ToString(defport, buf + 1, sizeof(buf) - 2);
    buf[buflen] = '\0';
    char* ptr = strstr(dest, buf);
    if (ptr && ptr[buflen] == 0) {
        len -= buflen;
        *ptr = 0;
    }
    strlwr(dest);
    return len;
}
