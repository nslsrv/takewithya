#include "codecs.h"
#include "common.h"
#include "legacy.h"

#include <contrib/libs/lz4/lz4.h>
#include <contrib/libs/lz4/lz4hc.h>
#include <contrib/libs/lz4/generated/iface.h>
#include <contrib/libs/fastlz/fastlz.h>
#include <contrib/libs/snappy/snappy.h>
#include <contrib/libs/zlib/zlib.h>
#include <contrib/libs/lzmasdk/LzmaLib.h>
#include <contrib/libs/libbz2/bzlib.h>

#define ZSTD_STATIC_LINKING_ONLY
#include <contrib/libs/zstd/zstd.h>

#include <util/ysaveload.h>
#include <util/stream/null.h>
#include <util/stream/mem.h>
#include <util/string/cast.h>
#include <util/string/join.h>
#include <util/system/align.h>
#include <util/system/unaligned_mem.h>
#include <util/generic/hash.h>
#include <util/generic/cast.h>
#include <util/generic/buffer.h>
#include <util/generic/region.h>
#include <util/generic/singleton.h>
#include <util/generic/algorithm.h>
#include <util/generic/mem_copy.h>

using namespace NBlockCodecs;

namespace {

    //lz4 codecs
    struct TLz4Base {
        static inline size_t DoMaxCompressedLength(size_t in) {
            return LZ4_compressBound(SafeIntegerCast<int>(in));
        }
    };

    struct TLz4FastCompress {
        inline TLz4FastCompress(int memory)
            : Memory(memory)
            , Methods(LZ4Methods(Memory))
        {
        }

        inline size_t DoCompress(const TData& in, void* buf) const {
            return Methods->LZ4Compress(~in, (char*)buf, +in);
        }

        inline TString CPrefix() {
            return "fast" + ToString(Memory);
        }

        const int Memory;
        const TLZ4Methods* Methods;
    };

    struct TLz4BestCompress {
        inline size_t DoCompress(const TData& in, void* buf) const {
            return LZ4_compressHC(~in, (char*)buf, +in);
        }

        static inline TString CPrefix() {
            return "hc";
        }
    };

    struct TLz4FastDecompress {
        inline void DoDecompress(const TData& in, void* out, size_t len) const {
            ssize_t res = LZ4_decompress_fast(~in, (char*)out, len);
            if (res < 0) {
                ythrow TDecompressError(res);
            }
        }

        static inline TStringBuf DPrefix() {
            return STRINGBUF("fast");
        }
    };

    struct TLz4SafeDecompress {
        inline void DoDecompress(const TData& in, void* out, size_t len) const {
            ssize_t res = LZ4_decompress_safe(~in, (char*)out, +in, len);
            if (res < 0) {
                ythrow TDecompressError(res);
            }
        }

        static inline TStringBuf DPrefix() {
            return STRINGBUF("safe");
        }
    };

    template <class TC, class TD>
    struct TLz4Codec: public TAddLengthCodec<TLz4Codec<TC, TD>>, public TLz4Base, public TC, public TD {
        inline TLz4Codec()
            : MyName("lz4-" + TC::CPrefix() + "-" + TD::DPrefix())
        {
        }

        template <class T>
        inline TLz4Codec(const T& t)
            : TC(t)
            , MyName("lz4-" + TC::CPrefix() + "-" + TD::DPrefix())
        {
        }

        TStringBuf Name() const noexcept override {
            return MyName;
        }

        const TString MyName;
    };

    //fastlz codecs
    struct TFastLZCodec: public TAddLengthCodec<TFastLZCodec> {
        inline TFastLZCodec(int level)
            : MyName("fastlz-" + ToString(level))
            , Level(level)
        {
        }

        static inline size_t DoMaxCompressedLength(size_t in) noexcept {
            return Max<size_t>(in + in / 20, 128);
        }

        TStringBuf Name() const noexcept override {
            return MyName;
        }

        inline size_t DoCompress(const TData& in, void* buf) const {
            if (Level) {
                return fastlz_compress_level(Level, ~in, +in, buf);
            }

            return fastlz_compress(~in, +in, buf);
        }

        inline void DoDecompress(const TData& in, void* out, size_t len) const {
            const int ret = fastlz_decompress(~in, +in, out, len);

            if (ret < 0 || (size_t)ret != len) {
                ythrow TDataError() << "can not decompress";
            }
        }

        const TString MyName;
        const int Level;
    };

    //snappy codec
    struct TSnappyCodec: public ICodec {
        size_t DecompressedLength(const TData& in) const override {
            size_t ret;

            if (snappy::GetUncompressedLength(~in, +in, &ret)) {
                return ret;
            }

            ythrow TDecompressError(0);
        }

        size_t MaxCompressedLength(const TData& in) const override {
            return snappy::MaxCompressedLength(+in);
        }

        size_t Compress(const TData& in, void* out) const override {
            size_t ret;

            snappy::RawCompress(~in, +in, (char*)out, &ret);

            return ret;
        }

        size_t Decompress(const TData& in, void* out) const override {
            if (snappy::RawUncompress(~in, +in, (char*)out)) {
                return DecompressedLength(in);
            }

            ythrow TDecompressError(0);
        }

        TStringBuf Name() const noexcept override {
            return "snappy";
        }
    };

    //zlib codecs
    struct TZLibCodec: public TAddLengthCodec<TZLibCodec> {
        inline TZLibCodec(int level)
            : MyName("zlib-" + ToString(level))
            , Level(level)
        {
        }

        static inline size_t DoMaxCompressedLength(size_t in) noexcept {
            return compressBound(in);
        }

        TStringBuf Name() const noexcept override {
            return MyName;
        }

        inline size_t DoCompress(const TData& in, void* buf) const {
            //TRASH detected
            uLong ret = Max<unsigned int>();

            int cres = compress2((Bytef*)buf, &ret, (const Bytef*)~in, +in, Level);

            if (cres != Z_OK) {
                ythrow TCompressError(cres);
            }

            return ret;
        }

        inline void DoDecompress(const TData& in, void* out, size_t len) const {
            uLong ret = len;

            int uncres = uncompress((Bytef*)out, &ret, (const Bytef*)~in, +in);
            if (uncres != Z_OK) {
                ythrow TDecompressError(uncres);
            }

            if (ret != len) {
                ythrow TDecompressError(len, ret);
            }
        }

        const TString MyName;
        const int Level;
    };

    //lzma codecs
    struct TLzmaCodec: public TAddLengthCodec<TLzmaCodec> {
        inline TLzmaCodec(int level)
            : Level(level)
            , MyName("lzma-" + ToString(Level))
        {
        }

        static inline size_t DoMaxCompressedLength(size_t in) noexcept {
            return Max<size_t>(in + in / 20, 128) + LZMA_PROPS_SIZE;
        }

        TStringBuf Name() const noexcept override {
            return MyName;
        }

        inline size_t DoCompress(const TData& in, void* buf) const {
            unsigned char* props = (unsigned char*)buf;
            unsigned char* data = props + LZMA_PROPS_SIZE;
            size_t destLen = Max<size_t>();
            size_t outPropsSize = LZMA_PROPS_SIZE;

            const int ret = LzmaCompress(data, &destLen, (const unsigned char*)~in, +in, props, &outPropsSize, Level, 0, -1, -1, -1, -1, -1);

            if (ret != SZ_OK) {
                ythrow TCompressError(ret);
            }

            return destLen + LZMA_PROPS_SIZE;
        }

        inline void DoDecompress(const TData& in, void* out, size_t len) const {
            const unsigned char* props = (const unsigned char*)~in;
            const unsigned char* data = props + LZMA_PROPS_SIZE;
            size_t destLen = len;
            SizeT srcLen = +in - LZMA_PROPS_SIZE;

            const int res = LzmaUncompress((unsigned char*)out, &destLen, data, &srcLen, props, LZMA_PROPS_SIZE);

            if (res != SZ_OK) {
                ythrow TDecompressError(res);
            }

            if (destLen != len) {
                ythrow TDecompressError(len, destLen);
            }
        }

        const int Level;
        const TString MyName;
    };

    //bzip2 codecs
    struct TBZipCodec: public TAddLengthCodec<TBZipCodec> {
        inline TBZipCodec(int level)
            : Level(level)
            , MyName("bzip2-" + ToString(Level))
        {
        }

        static inline size_t DoMaxCompressedLength(size_t in) noexcept {
            //rather strange
            return in * 2 + 128;
        }

        TStringBuf Name() const noexcept override {
            return MyName;
        }

        inline size_t DoCompress(const TData& in, void* buf) const {
            unsigned int ret = DoMaxCompressedLength(+in);
            const int res = BZ2_bzBuffToBuffCompress((char*)buf, &ret, (char*)~in, +in, Level, 0, 0);
            if (res != BZ_OK) {
                ythrow TCompressError(res);
            }

            return ret;
        }

        inline void DoDecompress(const TData& in, void* out, size_t len) const {
            unsigned int tmp = SafeIntegerCast<unsigned int>(len);
            const int res = BZ2_bzBuffToBuffDecompress((char*)out, &tmp, (char*)~in, +in, 0, 0);

            if (res != BZ_OK) {
                ythrow TDecompressError(res);
            }

            if (len != tmp) {
                ythrow TDecompressError(len, tmp);
            }
        }

        const int Level;
        const TString MyName;
    };

    struct TZStd08Codec: public TAddLengthCodec<TZStd08Codec> {
        inline TZStd08Codec(unsigned level)
            : Level(level)
            , MyName(STRINGBUF("zstd08_") + ToString(Level))
        {
        }

        static inline size_t CheckError(size_t ret, const char* what) {
            if (ZSTD_isError(ret)) {
                ythrow yexception() << what << STRINGBUF(" zstd error: ") << ZSTD_getErrorName(ret);
            }

            return ret;
        }

        static inline size_t DoMaxCompressedLength(size_t l) noexcept {
            return ZSTD_compressBound(l);
        }

        inline size_t DoCompress(const TData& in, void* out) const {
            return CheckError(ZSTD_compress(out, DoMaxCompressedLength(+in), ~in, +in, Level), "compress");
        }

        inline void DoDecompress(const TData& in, void* out, size_t dsize) const {
            const size_t res = CheckError(ZSTD_decompress(out, dsize, ~in, +in), "decompress");

            if (res != dsize) {
                ythrow TDecompressError(dsize, res);
            }
        }

        TStringBuf Name() const noexcept override {
            return MyName;
        }

        const unsigned Level;
        const TString MyName;
    };

    //end of codecs

    struct TCodecFactory {
        inline TCodecFactory() {
            Add(&Null);
            Add(&Snappy);

            for (int i = 0; i < 30; ++i) {
                typedef TLz4Codec<TLz4FastCompress, TLz4FastDecompress> T1;
                typedef TLz4Codec<TLz4FastCompress, TLz4SafeDecompress> T2;

                THolder<T1> t1(new T1(i));
                THolder<T2> t2(new T2(i));

                if (t1->Methods) {
                    Codecs.push_back(t1.Release());
                }

                if (t2->Methods) {
                    Codecs.push_back(t2.Release());
                }
            }

            Codecs.push_back(new TLz4Codec<TLz4BestCompress, TLz4FastDecompress>());
            Codecs.push_back(new TLz4Codec<TLz4BestCompress, TLz4SafeDecompress>());

            for (int i = 0; i < 3; ++i) {
                Codecs.push_back(new TFastLZCodec(i));
            }

            for (int i = 0; i < 10; ++i) {
                Codecs.push_back(new TZLibCodec(i));
            }

            for (int i = 1; i < 10; ++i) {
                Codecs.push_back(new TBZipCodec(i));
            }

            for (int i = 0; i < 10; ++i) {
                Codecs.push_back(new TLzmaCodec(i));
            }

            Codecs.push_back(LegacyZStdCodec());

            for (auto& codec : LegacyZStd06Codec()) {
                Codecs.emplace_back(std::move(codec));
            }

            for (int i = 1; i <= ZSTD_maxCLevel(); ++i) {
                Codecs.push_back(new TZStd08Codec(i));
            }

            for (size_t i = 0; i < +Codecs; ++i) {
                Add(Codecs[i].Get());
            }

            //aliases
            Registry["fastlz"] = Registry["fastlz-0"];
            Registry["zlib"] = Registry["zlib-6"];
            Registry["bzip2"] = Registry["bzip2-6"];
            Registry["lzma"] = Registry["lzma-5"];
            Registry["lz4-fast-safe"] = Registry["lz4-fast14-safe"];
            Registry["lz4-fast-fast"] = Registry["lz4-fast14-fast"];
            Registry["lz4"] = Registry["lz4-fast-safe"];
            Registry["lz4fast"] = Registry["lz4-fast-fast"];
            Registry["lz4hc"] = Registry["lz4-hc-safe"];
        }

        inline const ICodec* Find(const TStringBuf& name) const {
            TRegistry::const_iterator it = Registry.find(name);

            if (it == Registry.end()) {
                ythrow TNotFound() << "can not found " << name << " codec";
            }

            return it->second;
        }

        inline void ListCodecs(TCodecList& lst) const {
            for (const auto& it : Registry) {
                lst.push_back(it.first);
            }

            Sort(lst.begin(), lst.end());
        }

        inline void Add(ICodec* codec) {
            Registry[codec->Name()] = codec;
        }

        TNullCodec Null;
        TSnappyCodec Snappy;
        yvector<TCodecPtr> Codecs;
        typedef yhash<TStringBuf, ICodec*> TRegistry;
        TRegistry Registry;
    };
}

const ICodec* NBlockCodecs::Codec(const TStringBuf& name) {
    return Singleton<TCodecFactory>()->Find(name);
}

TCodecList NBlockCodecs::ListAllCodecs() {
    TCodecList ret;

    Singleton<TCodecFactory>()->ListCodecs(ret);

    return ret;
}

TString NBlockCodecs::ListAllCodecsAsString() {
    return JoinSeq(STRINGBUF(","), ListAllCodecs());
}

void ICodec::Encode(const TData& in, TBuffer& out) const {
    const size_t maxLen = MaxCompressedLength(in);

    out.Reserve(maxLen);
    out.Resize(Compress(in, out.Data()));
}

void ICodec::Decode(const TData& in, TBuffer& out) const {
    const size_t len = DecompressedLength(in);

    out.Reserve(len);
    out.Resize(Decompress(in, out.Data()));
}

void ICodec::Encode(const TData& in, TString& out) const {
    const size_t maxLen = MaxCompressedLength(in);

    out.reserve(maxLen);
    out.ReserveAndResize(Compress(in, out.begin()));
}

void ICodec::Decode(const TData& in, TString& out) const {
    const size_t len = DecompressedLength(in);

    out.reserve(len);
    out.ReserveAndResize(Decompress(in, out.begin()));
}

ICodec::~ICodec() = default;
