#include "fast.h"
#include "random.h"
#include "entropy.h"
#include "mersenne.h"
#include "shuffle.h"

#include <util/stream/output.h>
#include <util/stream/mem.h>
#include <util/stream/zlib.h>
#include <util/stream/buffer.h>
#include <util/system/info.h>
#include <util/system/spinlock.h>
#include <util/system/thread.h>
#include <util/system/execpath.h>
#include <util/system/datetime.h>
#include <util/system/hostname.h>
#include <util/system/unaligned_mem.h>
#include <util/generic/buffer.h>
#include <util/generic/singleton.h>
#include <util/digest/murmur.h>
#include <util/datetime/cputimer.h>
#include <util/ysaveload.h>

namespace {
    static inline void Permute(char* buf, size_t len) noexcept {
        Shuffle(buf, buf + len, TReallyFastRng32(*buf + len));
    }

    struct THostEntropy: public TBuffer {
        inline THostEntropy() {
            {
                TBufferOutput buf(*this);
                TZLibCompress out(&buf);

                Save(&out, GetCycleCount());
                Save(&out, MicroSeconds());
                Save(&out, TThread::CurrentThreadId());
                Save(&out, NSystemInfo::CachedNumberOfCpus());
                Save(&out, HostName());

                try {
                    Save(&out, GetExecPath());
                } catch (...) {
                    //workaround - sometimes fails on FreeBSD
                }

                Save(&out, (size_t)Data());

                double la[3];

                NSystemInfo::LoadAverage(la, Y_ARRAY_SIZE(la));

                out.Write(la, sizeof(la));
            }

            {
                TMemoryOutput out(Data(), Size());

                //replace zlib header with hash
                Save(&out, MurmurHash<ui64>(Data(), Size()));
            }

            Permute(Data(), Size());
        }
    };

    //not thread-safe
    class TMersenneInput: public TInputStream {
        using TKey = ui64;
        using TRnd = TMersenne<TKey>;

    public:
        inline TMersenneInput(const TBuffer& rnd)
            : Rnd_((const TKey*)rnd.Data(), rnd.Size() / sizeof(TKey))
        {
        }

        ~TMersenneInput() override = default;

        size_t DoRead(void* buf, size_t len) override {
            size_t toRead = len;

            while (toRead) {
                const TKey next = Rnd_.GenRand();
                const size_t toCopy = Min(toRead, sizeof(next));

                memcpy(buf, &next, toCopy);

                buf = (char*)buf + toCopy;
                toRead -= toCopy;
            }

            return len;
        }

    private:
        TRnd Rnd_;
    };

    class TEntropyPoolStream: public TInputStream {
    public:
        inline TEntropyPoolStream(const TBuffer& buffer)
            : Mi_(buffer)
            , Bi_(&Mi_, 8192)
        {
        }

        size_t DoRead(void* buf, size_t len) override {
            auto guard = Guard(Mutex_);

            return Bi_.Read(buf, len);
        }

    private:
        TAdaptiveLock Mutex_;
        TMersenneInput Mi_;
        TBufferedInput Bi_;
    };

    struct TSeedStream: public TInputStream {
        size_t DoRead(void* inbuf, size_t len) override {
            char* buf = (char*)inbuf;

#define DO_STEP(type)                              \
    while (len >= sizeof(type)) {                  \
        WriteUnaligned(buf, RandomNumber<type>()); \
        buf += sizeof(type);                       \
        len -= sizeof(type);                       \
    }

            DO_STEP(ui64);
            DO_STEP(ui32);
            DO_STEP(ui16);
            DO_STEP(ui8);

#undef DO_STEP

            return buf - (char*)inbuf;
        }
    };

    struct TDefaultTraits {
        const THostEntropy HE;
        TEntropyPoolStream EP;
        TSeedStream SS;

        inline TDefaultTraits()
            : EP(HE)
        {
        }

        inline const TBuffer& HostEntropy() const noexcept {
            return HE;
        }

        inline TInputStream& EntropyPool() noexcept {
            return EP;
        }

        inline TInputStream& Seed() noexcept {
            return SS;
        }

        static inline TDefaultTraits& Instance() {
            return *SingletonWithPriority<TDefaultTraits, 0>();
        }
    };

    using TRandomTraits = TDefaultTraits;
}

TInputStream& EntropyPool() {
    return TRandomTraits::Instance().EntropyPool();
}

TInputStream& Seed() {
    return TRandomTraits::Instance().Seed();
}

const TBuffer& HostEntropy() {
    return TRandomTraits::Instance().HostEntropy();
}
