#pragma once

#include "output.h"
#include "zerocopy.h"

#include <utility>
#include <util/generic/ptr.h>
#include <util/generic/typetraits.h>
#include <util/generic/store_policy.h>

/**
 * @addtogroup Streams_Buffered
 * @{
 */

/**
 * Input stream that wraps the given stream and adds a buffer on top of it,
 * thus making sure that data is read from the underlying stream in big chunks.
 *
 * Note that it does not claim ownership of the underlying stream, so it's up
 * to the user to free it.
 */
class TBufferedInput: public TZeroCopyInput {
public:
    TBufferedInput(TInputStream* slave, size_t buflen = 8192);
    ~TBufferedInput() override;

    /**
     * Switches the underlying stream to the one provided. Does not clear the
     * data that was already buffered.
     *
     * @param slave                     New underlying stream.
     */
    void Reset(TInputStream* slave);

protected:
    size_t DoRead(void* buf, size_t len) override;
    size_t DoReadTo(TString& st, char ch) override;
    size_t DoSkip(size_t len) override;
    size_t DoNext(const void** ptr, size_t len) override;

private:
    class TImpl;
    THolder<TImpl> Impl_;
};

/**
 * Output stream that wraps the given stream and adds a buffer on top of it,
 * thus making sure that data is written to the underlying stream in big chunks.
 *
 * Note that by default this stream does not propagate `Flush` and `Finish`
 * calls to the underlying stream, instead simply flushing out the buffer.
 * You can change this behavior by using propagation mode setters.
 *
 * Also note that this stream does not claim ownership of the underlying stream,
 * so it's up to the user to free it.
 */
class TBufferedOutputBase: public TOutputStream {
public:
    /**
     * Constructs a buffered stream that dynamically adjusts the size of the
     * buffer. This works best when the amount of data that will be passed
     * through this stream is not known and can range in size from several
     * kilobytes to several gigabytes.
     *
     * @param slave                     Underlying stream.
     */
    TBufferedOutputBase(TOutputStream* slave);

    /**
     * Constructs a buffered stream with the given size of the buffer.
     *
     * @param slave                     Underlying stream.
     * @param buflen                    Size of the buffer.
     */
    TBufferedOutputBase(TOutputStream* slave, size_t buflen);

    TBufferedOutputBase(TBufferedOutputBase&&) noexcept = default;
    TBufferedOutputBase& operator=(TBufferedOutputBase&&) noexcept = default;

    ~TBufferedOutputBase() override;

    /**
     * @param propagate                 Whether `Flush` and `Finish` calls should
     *                                  be propagated to the underlying stream.
     *                                  By default they are not.
     */
    inline void SetPropagateMode(bool propagate) noexcept {
        SetFlushPropagateMode(propagate);
        SetFinishPropagateMode(propagate);
    }

    /**
     * @param propagate                 Whether `Flush` calls should be propagated
     *                                  to the underlying stream. By default they
     *                                  are not.
     */
    void SetFlushPropagateMode(bool propagate) noexcept;

    /**
     * @param propagate                 Whether `Finish` calls should be propagated
     *                                  to the underlying stream. By default they
     *                                  are not.
     */
    void SetFinishPropagateMode(bool propagate) noexcept;

    class TImpl;

protected:
    void DoWrite(const void* data, size_t len) override;
    void DoFlush() override;
    void DoFinish() override;

private:
    THolder<TImpl> Impl_;
};

/**
 * Buffered output stream with a fixed-size buffer.
 *
 * @see TBufferedOutputBase
 */
class TBufferedOutput: public TBufferedOutputBase {
public:
    TBufferedOutput(TOutputStream* slave, size_t buflen = 8192);
    ~TBufferedOutput() override;

    TBufferedOutput(TBufferedOutput&&) noexcept = default;
    TBufferedOutput& operator=(TBufferedOutput&&) noexcept = default;
};

/**
 * Buffered output stream that dynamically adjusts the size of the buffer based
 * on the amount of data that's passed through it.
 *
 * @see TBufferedOutputBase
 */
class TAdaptiveBufferedOutput: public TBufferedOutputBase {
public:
    TAdaptiveBufferedOutput(TOutputStream* slave);
    ~TAdaptiveBufferedOutput() override;

    TAdaptiveBufferedOutput(TAdaptiveBufferedOutput&&) noexcept = default;
    TAdaptiveBufferedOutput& operator=(TAdaptiveBufferedOutput&&) noexcept = default;
};

namespace NPrivate {
    struct TMyBufferedOutput: public TBufferedOutput {
        inline TMyBufferedOutput(TOutputStream* slave, size_t buflen)
            : TBufferedOutput(slave, buflen)
        {
            SetFinishPropagateMode(true);
        }
    };

    template <class T>
    struct TBufferedStreamFor {
        using TResult = std::conditional_t<std::is_base_of<TInputStream, T>::value, TBufferedInput, TMyBufferedOutput>;
    };
}

/**
 * A mixin class that turns unbuffered stream into a buffered one.
 *
 * Note that using this mixin with a stream that is already buffered won't
 * result in double buffering, e.g. `TBuffered<TBuffered<TFileInput>>` and
 * `TBuffered<TFileInput>` are basically the same types.
 *
 * Example usage:
 * @code
 * TBuffered<TFileInput> file_input(1024, "/path/to/file");
 * TBuffered<TFileOutput> file_output(1024, "/path/to/file");
 * @endcode
 * Here 1024 is the size of the buffer.
 */
template <class TSlave>
class TBuffered: private TEmbedPolicy<TSlave>, public ::NPrivate::TBufferedStreamFor<TSlave>::TResult {
    using TSlaveBase = TEmbedPolicy<TSlave>;
    using TBufferedBase = typename ::NPrivate::TBufferedStreamFor<TSlave>::TResult;

public:
    template <typename... Args>
    inline TBuffered(size_t b, Args&&... args)
        : TSlaveBase(std::forward<Args>(args)...)
        , TBufferedBase(TSlaveBase::Ptr(), b)
    {
    }

    inline TSlave& Slave() noexcept {
        return *this->Ptr();
    }
};

/**
 * A mixin class that turns unbuffered stream into an adaptively buffered one.
 * Created stream differs from the one created via `TBuffered` template in that
 * it dynamically adjusts the size of the buffer based on the amount of data
 * that's passed through it.
 *
 * Example usage:
 * @code
 * TAdaptivelyBuffered<TFileOutput> file_output("/path/to/file");
 * @endcode
 */
template <class TSlave>
class TAdaptivelyBuffered: private TEmbedPolicy<TSlave>, public TAdaptiveBufferedOutput {
    using TSlaveBase = TEmbedPolicy<TSlave>;

public:
    template <typename... Args>
    inline TAdaptivelyBuffered(Args&&... args)
        : TSlaveBase(std::forward<Args>(args)...)
        , TAdaptiveBufferedOutput(TSlaveBase::Ptr())
    {
        this->SetFinishPropagateMode(true);
    }
};

/** @} */
