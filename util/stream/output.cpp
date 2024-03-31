#include "output.h"

#include <util/string/cast.h>
#include "format.h"
#include <util/memory/tempbuf.h>
#include <util/generic/singleton.h>
#include <util/generic/yexception.h>
#include <util/charset/wide.h>

#if defined(_android_)
#include <util/system/dynlib.h>
#include <android/log.h>
#endif

#include <cerrno>
#include <string>
#include <cstdio>

#if defined(_win_)
#include <io.h>
#endif

TOutputStream::TOutputStream() noexcept = default;

TOutputStream::~TOutputStream() = default;

void TOutputStream::DoFlush() {
    /*
     * do nothing
     */
}

void TOutputStream::DoFinish() {
    Flush();
}

void TOutputStream::DoWriteV(const TPart* parts, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const TPart& part = parts[i];

        DoWrite(part.buf, part.len);
    }
}

static void WriteString(TOutputStream& o, const wchar16* w, size_t n) {
    const size_t buflen = (n * 4); // * 4 because the conversion functions can convert unicode character into maximum 4 bytes of UTF8
    TTempBuf buffer(buflen + 1);
    char* const data = buffer.Data();
    size_t written = 0;
    WideToUTF8(w, n, data, written);
    data[written] = 0;
    o.Write(data, written);
}

template <>
void Out<TString>(TOutputStream& o, const TString& p) {
    o.Write(~p, +p);
}

template <>
void Out<std::string>(TOutputStream& o, const std::string& p) {
    o.Write(p.data(), p.length());
}

template <>
void Out<TFixedString<char>>(TOutputStream& o, const TFixedString<char>& p) {
    o.Write(p.Start, p.Length);
}

template <>
void Out<TFixedString<wchar16>>(TOutputStream& o, const TFixedString<wchar16>& p) {
    WriteString(o, p.Start, p.Length);
}

template <>
void Out<const wchar16*>(TOutputStream& o, const wchar16* w) {
    if (w) {
        WriteString(o, w, TCharTraits<wchar16>::GetLength(w));
    } else {
        o.Write("(null)");
    }
}

template <>
void Out<TUtf16String>(TOutputStream& o, const TUtf16String& w) {
    WriteString(o, w.c_str(), w.size());
}

#define DEF_CONV_DEFAULT(type)                  \
    template <>                                 \
    void Out<type>(TOutputStream & o, type p) { \
        o << ToString(p);                       \
    }

#define DEF_CONV_CHR(type)                      \
    template <>                                 \
    void Out<type>(TOutputStream & o, type p) { \
        o.Write((const char*)&p, 1);            \
    }

#define DEF_CONV_NUM(type, len)                                   \
    template <>                                                   \
    void Out<type>(TOutputStream & o, type p) {                   \
        char buf[len];                                            \
        o.Write(buf, ToString(p, buf, sizeof(buf)));              \
    }                                                             \
                                                                  \
    template <>                                                   \
    void Out<volatile type>(TOutputStream & o, volatile type p) { \
        Out<type>(o, p);                                          \
    }

DEF_CONV_NUM(bool, 64)

DEF_CONV_CHR(char)
DEF_CONV_CHR(signed char)
DEF_CONV_CHR(unsigned char)

DEF_CONV_NUM(signed short, 64)
DEF_CONV_NUM(signed int, 64)
DEF_CONV_NUM(signed long int, 64)
DEF_CONV_NUM(signed long long int, 64)

DEF_CONV_NUM(unsigned short, 64)
DEF_CONV_NUM(unsigned int, 64)
DEF_CONV_NUM(unsigned long int, 64)
DEF_CONV_NUM(unsigned long long int, 64)

DEF_CONV_NUM(float, 512)
DEF_CONV_NUM(double, 512)
DEF_CONV_NUM(long double, 512)

template <>
void Out<TBasicCharRef<TString>>(TOutputStream& o, const TBasicCharRef<TString>& c) {
    o << static_cast<char>(c);
}

template <>
void Out<TBasicCharRef<TUtf16String>>(TOutputStream& o, const TBasicCharRef<TUtf16String>& c) {
    o << static_cast<wchar16>(c);
}

template <>
void Out<const void*>(TOutputStream& o, const void* t) {
    o << Hex(size_t(t));
}

template <>
void Out<void*>(TOutputStream& o, void* t) {
    Out<const void*>(o, t);
}

using TNullPtr = decltype(nullptr);

template <>
void Out<TNullPtr>(TOutputStream& o, TTypeTraits<TNullPtr>::TFuncParam) {
    o << STRINGBUF("nullptr");
}

#if defined(_android_)
namespace {
    class TAndroidStdIOStreams {
    public:
        TAndroidStdIOStreams()
            : LogLibrary("liblog.so")
            , LogFuncPtr((TLogFuncPtr)LogLibrary.Sym("__android_log_write"))
            , Out(LogFuncPtr)
            , Err(LogFuncPtr)
        {
        }

    public:
        using TLogFuncPtr = void (*)(int, const char*, const char*);

        class TAndroidStdOutput: public TOutputStream {
        public:
            inline TAndroidStdOutput(TLogFuncPtr logFuncPtr) noexcept
                : Buffer()
                , LogFuncPtr(logFuncPtr)
            {
            }

            virtual ~TAndroidStdOutput() {
            }

        private:
            virtual void DoWrite(const void* buf, size_t len) override {
                Buffer.Write(buf, len);
            }

            virtual void DoFlush() override {
                LogFuncPtr(ANDROID_LOG_DEBUG, GetTag(), Buffer.Data());
                Buffer.Clear();
            }

            virtual const char* GetTag() const = 0;

        private:
            TStringStream Buffer;
            TLogFuncPtr LogFuncPtr;
        };

        class TStdErr: public TAndroidStdOutput {
        public:
            TStdErr(TLogFuncPtr logFuncPtr)
                : TAndroidStdOutput(logFuncPtr)
            {
            }

            virtual ~TStdErr() {
            }

        private:
            virtual const char* GetTag() const override {
                return "stderr";
            }
        };

        class TStdOut: public TAndroidStdOutput {
        public:
            TStdOut(TLogFuncPtr logFuncPtr)
                : TAndroidStdOutput(logFuncPtr)
            {
            }

            virtual ~TStdOut() {
            }

        private:
            virtual const char* GetTag() const override {
                return "stdout";
            }
        };

        static bool Enabled;
        TDynamicLibrary LogLibrary; // field order is important, see constructor
        TLogFuncPtr LogFuncPtr;
        TStdOut Out;
        TStdErr Err;

        static inline TAndroidStdIOStreams& Instance() {
            return *SingletonWithPriority<TAndroidStdIOStreams, 4>();
        }
    };

    bool TAndroidStdIOStreams::Enabled = false;
}
#endif // _android_

namespace {
    class TStdOutput: public TOutputStream {
    public:
        inline TStdOutput(FILE* f) noexcept
            : F_(f)
        {
        }

        ~TStdOutput() override = default;

    private:
        void DoWrite(const void* buf, size_t len) override {
            if (len != fwrite(buf, 1, len, F_)) {
#if defined(_win_)
                // On Windows, if 'F_' is console -- 'fwrite' returns count of written characters.
                // If, for example, console output codepage is UTF-8, then returned value is
                // not equal to 'len'. So, we ignore some 'errno' values...
                if ((errno == 0 || errno == EINVAL || errno == EILSEQ) && _isatty(fileno(F_))) {
                    return;
                }
#endif
                ythrow TSystemError() << "write failed";
            }
        }

        void DoFlush() override {
            if (fflush(F_) != 0) {
                ythrow TSystemError() << "fflush failed";
            }
        }

    private:
        FILE* F_;
    };

    struct TStdIOStreams {
        struct TStdErr: public TStdOutput {
            inline TStdErr()
                : TStdOutput(stderr)
            {
            }

            ~TStdErr() override = default;
        };

        struct TStdOut: public TStdOutput {
            inline TStdOut()
                : TStdOutput(stdout)
            {
            }

            ~TStdOut() override = default;
        };

        TStdOut Out;
        TStdErr Err;

        static inline TStdIOStreams& Instance() {
            return *SingletonWithPriority<TStdIOStreams, 4>();
        }
    };
}

TOutputStream& NPrivate::StdErrStream() noexcept {
#if defined(_android_)
    if (TAndroidStdIOStreams::Enabled) {
        return TAndroidStdIOStreams::Instance().Err;
    }
#endif
    return TStdIOStreams::Instance().Err;
}

TOutputStream& NPrivate::StdOutStream() noexcept {
#if defined(_android_)
    if (TAndroidStdIOStreams::Enabled) {
        return TAndroidStdIOStreams::Instance().Out;
    }
#endif
    return TStdIOStreams::Instance().Out;
}

void RedirectStdioToAndroidLog(bool redirect) {
#if defined(_android_)
    TAndroidStdIOStreams::Enabled = redirect;
#else
    Y_UNUSED(redirect);
#endif
}
