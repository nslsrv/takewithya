#include "mem.h"
#include "buffered.h"

#include <util/memory/addstorage.h>
#include <util/generic/yexception.h>
#include <util/generic/buffer.h>

class TBufferedInput::TImpl: public TAdditionalStorage<TImpl> {
public:
    inline TImpl(TInputStream* slave)
        : Slave_(slave)
        , MemInput_(nullptr, 0)
    {
    }

    inline ~TImpl() = default;

    inline size_t Next(const void** ptr, size_t len) {
        if (MemInput_.Exhausted()) {
            MemInput_.Reset(Buf(), Slave_->Read(Buf(), BufLen()));
        }

        return MemInput_.Next(ptr, len);
    }

    inline size_t Read(void* buf, size_t len) {
        if (MemInput_.Exhausted()) {
            if (len > BufLen() / 2) {
                return Slave_->Read(buf, len);
            }

            MemInput_.Reset(Buf(), Slave_->Read(Buf(), BufLen()));
        }

        return MemInput_.Read(buf, len);
    }

    inline size_t Skip(size_t len) {
        size_t totalSkipped = 0;
        while (len) {
            const size_t skipped = DoSkip(len);
            if (skipped == 0) {
                break;
            }

            totalSkipped += skipped;
            len -= skipped;
        }

        return totalSkipped;
    }

    inline size_t DoSkip(size_t len) {
        if (MemInput_.Exhausted()) {
            if (len > BufLen() / 2) {
                return Slave_->Skip(len);
            }

            MemInput_.Reset(Buf(), Slave_->Read(Buf(), BufLen()));
        }

        return MemInput_.Skip(len);
    }

    inline size_t ReadTo(TString& st, char to) {
        TString res;
        TString s_tmp;

        size_t ret = 0;

        while (true) {
            if (MemInput_.Exhausted()) {
                const size_t readed = Slave_->Read(Buf(), BufLen());

                if (!readed) {
                    break;
                }

                MemInput_.Reset(Buf(), readed);
            }

            const size_t a_len(MemInput_.Avail());
            ret += MemInput_.ReadTo(s_tmp, to);
            const size_t s_len = s_tmp.length();

            /*
             * mega-optimization
             */
            if (res.empty()) {
                res.swap(s_tmp);
            } else {
                res += s_tmp;
            }

            if (s_len != a_len) {
                break;
            }
        }

        st.swap(res);

        return ret;
    }

    inline void Reset(TInputStream* slave) {
        Slave_ = slave;
    }

private:
    inline size_t BufLen() const noexcept {
        return AdditionalDataLength();
    }

    inline void* Buf() const noexcept {
        return AdditionalData();
    }

private:
    TInputStream* Slave_;
    TMemoryInput MemInput_;
};

TBufferedInput::TBufferedInput(TInputStream* slave, size_t buflen)
    : Impl_(new (buflen) TImpl(slave))
{
}

TBufferedInput::~TBufferedInput() = default;

size_t TBufferedInput::DoRead(void* buf, size_t len) {
    return Impl_->Read(buf, len);
}

size_t TBufferedInput::DoSkip(size_t len) {
    return Impl_->Skip(len);
}

size_t TBufferedInput::DoNext(const void** ptr, size_t len) {
    return Impl_->Next(ptr, len);
}

size_t TBufferedInput::DoReadTo(TString& st, char ch) {
    return Impl_->ReadTo(st, ch);
}

void TBufferedInput::Reset(TInputStream* slave) {
    Impl_->Reset(slave);
}

class TBufferedOutputBase::TImpl {
public:
    inline TImpl(TOutputStream* slave)
        : Slave_(slave)
        , MemOut_(nullptr, 0)
        , PropagateFlush_(false)
        , PropagateFinish_(false)
    {
    }

    virtual ~TImpl() = default;

    inline void Reset() {
        MemOut_.Reset(Buf(), Len());
    }

    inline void Write(const void* buf, size_t len) {
        if (len <= MemOut_.Avail()) {
            /*
             * fast path
             */

            MemOut_.Write(buf, len);
        } else {
            const size_t stored = Stored();
            const size_t full_len = stored + len;
            const size_t good_len = DownToBufferGranularity(full_len);
            const size_t write_from_buf = good_len - stored;

            using TPart = TOutputStream::TPart;

            alignas(TPart) char data[2 * sizeof(TPart)];
            TPart* parts = reinterpret_cast<TPart*>(data);
            TPart* end = parts;

            if (stored) {
                new (end++) TPart(Buf(), stored);
            }

            if (write_from_buf) {
                new (end++) TPart(buf, write_from_buf);
            }

            Slave_->Write(parts, end - parts);

            //grow buffer only on full flushes
            OnBufferExhausted();
            Reset();

            if (write_from_buf < len) {
                MemOut_.Write((const char*)buf + write_from_buf, len - write_from_buf);
            }
        }
    }

    inline void SetFlushPropagateMode(bool mode) noexcept {
        PropagateFlush_ = mode;
    }

    inline void SetFinishPropagateMode(bool mode) noexcept {
        PropagateFinish_ = mode;
    }

    inline void Flush() {
        {
            Slave_->Write(Buf(), Stored());
            Reset();
        }

        if (PropagateFlush_) {
            Slave_->Flush();
        }
    }

    inline void Finish() {
        try {
            Flush();
        } catch (...) {
            try {
                DoFinish();
            } catch (...) {
            }

            throw;
        }

        DoFinish();
    }

private:
    inline void DoFinish() {
        if (PropagateFinish_) {
            Slave_->Finish();
        }
    }

    inline size_t Stored() const noexcept {
        return Len() - MemOut_.Avail();
    }

    inline size_t DownToBufferGranularity(size_t l) const noexcept {
        return l - (l % Len());
    }

    virtual void OnBufferExhausted() = 0;
    virtual void* Buf() const noexcept = 0;
    virtual size_t Len() const noexcept = 0;

private:
    TOutputStream* Slave_;
    TMemoryOutput MemOut_;
    bool PropagateFlush_;
    bool PropagateFinish_;
};

namespace {
    struct TSimpleImpl: public TBufferedOutputBase::TImpl, public TAdditionalStorage<TSimpleImpl> {
        inline TSimpleImpl(TOutputStream* slave)
            : TBufferedOutputBase::TImpl(slave)
        {
            Reset();
        }

        ~TSimpleImpl() override = default;

        void OnBufferExhausted() final {
        }

        void* Buf() const noexcept override {
            return AdditionalData();
        }

        size_t Len() const noexcept override {
            return AdditionalDataLength();
        }
    };

    struct TAdaptiveImpl: public TBufferedOutputBase::TImpl {
        enum {
            Step = 4096
        };

        inline TAdaptiveImpl(TOutputStream* slave)
            : TBufferedOutputBase::TImpl(slave)
            , N_(0)
        {
            B_.Reserve(Step);
            Reset();
        }

        ~TAdaptiveImpl() override = default;

        void OnBufferExhausted() final {
            const size_t c = ((size_t)Step) << Min<size_t>(++N_ / 32, 10);

            if (c > B_.Capacity()) {
                TBuffer(c).Swap(B_);
            }
        }

        void* Buf() const noexcept override {
            return (void*)B_.Data();
        }

        size_t Len() const noexcept override {
            return B_.Capacity();
        }

        TBuffer B_;
        ui64 N_;
    };
}

TBufferedOutputBase::TBufferedOutputBase(TOutputStream* slave)
    : Impl_(new TAdaptiveImpl(slave))
{
}

TBufferedOutputBase::TBufferedOutputBase(TOutputStream* slave, size_t buflen)
    : Impl_(new (buflen) TSimpleImpl(slave))
{
}

TBufferedOutputBase::~TBufferedOutputBase() {
    try {
        Finish();
    } catch (...) {
    }
}

void TBufferedOutputBase::DoWrite(const void* data, size_t len) {
    if (Impl_.Get()) {
        Impl_->Write(data, len);
    } else {
        ythrow yexception() << "can not write to finished stream";
    }
}

void TBufferedOutputBase::DoFlush() {
    if (Impl_.Get()) {
        Impl_->Flush();
    }
}

void TBufferedOutputBase::DoFinish() {
    THolder<TImpl> impl(Impl_.Release());

    if (impl) {
        impl->Finish();
    }
}

void TBufferedOutputBase::SetFlushPropagateMode(bool propagate) noexcept {
    if (Impl_.Get()) {
        Impl_->SetFlushPropagateMode(propagate);
    }
}

void TBufferedOutputBase::SetFinishPropagateMode(bool propagate) noexcept {
    if (Impl_.Get()) {
        Impl_->SetFinishPropagateMode(propagate);
    }
}

TBufferedOutput::TBufferedOutput(TOutputStream* slave, size_t buflen)
    : TBufferedOutputBase(slave, buflen)
{
}

TBufferedOutput::~TBufferedOutput() = default;

TAdaptiveBufferedOutput::TAdaptiveBufferedOutput(TOutputStream* slave)
    : TBufferedOutputBase(slave)
{
}

TAdaptiveBufferedOutput::~TAdaptiveBufferedOutput() = default;
