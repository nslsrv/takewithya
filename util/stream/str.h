#pragma once

#include "output.h"
#include "zerocopy.h"

#include <util/generic/string.h>
#include <util/generic/noncopyable.h>
#include <util/generic/store_policy.h>

/**
 * @addtogroup Streams_Strings
 * @{
 */

/**
 * Input stream for reading data from a string.
 */
class TStringInput: public TZeroCopyInputFastReadTo {
public:
    /**
     * Constructs a string input stream that reads character data from the
     * provided string.
     *
     * Note that this stream keeps a reference to the provided string, so it's
     * up to the user to make sure that the string doesn't get destroyed while
     * this stream is in use.
     *
     * For reading data from `TStringBuf`s, see `TMemoryInput` (`util/stream/mem.h`).
     *
     * @param s                         String to read from.
     */
    inline TStringInput(const TString& s) noexcept
        : S_(s)
        , Pos_(0)
    {
    }

    TStringInput(const TString&&) = delete;

    ~TStringInput() override;

    TStringInput(TStringInput&&) noexcept = default;
    TStringInput& operator=(TStringInput&&) noexcept = default;

protected:
    size_t DoNext(const void** ptr, size_t len) override;
    void DoUndo(size_t len) override;

private:
    const TString& S_;
    size_t Pos_;

    friend class TStringStream;
};

/**
 * Stream for writing data into a string.
 */
class TStringOutput: public TOutputStream {
public:
    /**
     * Constructs a string output stream that appends character data to the
     * provided string.
     *
     * Note that this stream keeps a reference to the provided string, so it's
     * up to the user to make sure that the string doesn't get destroyed while
     * this stream is in use.
     *
     * @param s                         String to append to.
     */
    inline TStringOutput(TString& s) noexcept
        : S_(s)
    {
    }

    ~TStringOutput() override;

    /**
     * @param size                      Number of additional characters to
     *                                  reserve in output string.
     */
    inline void Reserve(size_t size) {
        S_.reserve(S_.size() + size);
    }

private:
    void DoWrite(const void* buf, size_t len) override;

private:
    TString& S_;
};

/**
 * String input/output stream, similar to `std::stringstream`.
 */
class TStringStream: private TEmbedPolicy<TString>, public TStringInput, public TStringOutput {
    using TEmbededString = TEmbedPolicy<TString>;

public:
    inline TStringStream()
        : TEmbededString()
        , TStringInput(*TEmbededString::Ptr())
        , TStringOutput(*TEmbededString::Ptr())
    {
    }

    inline TStringStream(const TString& string)
        : TEmbededString(string)
        , TStringInput(*TEmbededString::Ptr())
        , TStringOutput(*TEmbededString::Ptr())
    {
    }

    inline TStringStream(const TStringStream& other)
        : TEmbededString(other.Str())
        , TStringInput(*TEmbededString::Ptr())
        , TStringOutput(*TEmbededString::Ptr())
    {
    }

    inline TStringStream& operator=(const TStringStream& other) {
        // All references remain alive, we need to change position only
        Str() = other.Str();
        Pos_ = other.Pos_;
        return *this;
    }

    ~TStringStream() override;

    /**
     * @returns                         Whether @c this contains any data
     */
    explicit operator bool() const noexcept {
        return !Empty();
    }

    /**
     * @returns                         String that this stream is writing into.
     */
    inline TString& Str() noexcept {
        return *Ptr();
    }

    /**
     * @returns                         String that this stream is writing into.
     */
    inline const TString& Str() const noexcept {
        return *Ptr();
    }

    /**
     * @returns                         Pointer to the character data contained
     *                                  in this stream. The data is guaranteed
     *                                  to be null-terminated.
     */
    inline const char* Data() const noexcept {
        return Ptr()->Data();
    }

    /**
     * @see Data()
     */
    inline const char* operator~() const noexcept {
        return Data();
    }

    /**
     * @returns                         Total number of characters in this
     *                                  stream. Note that this is not the same
     *                                  as the total number of characters
     *                                  available for reading.
     */
    inline size_t Size() const noexcept {
        return Ptr()->Size();
    }

    /**
     * @see Size()
     */
    inline size_t operator+() const noexcept {
        return Size();
    }

    /**
     * @returns                         Whether the string that this stream
     *                                  operates on is empty.
     */
    inline bool Empty() const noexcept {
        return Str().empty();
    }

    using TStringOutput::Reserve;

    /**
     * Clears the string that this stream operates on and resets the
     * read/write pointers.
     */
    inline void Clear() {
        Str().clear();
        Pos_ = 0;
    }

    // TODO: compatibility with existing code, remove

    bool empty() const {
        return Empty();
    }

    void clear() {
        Clear();
    }
};

/** @} */
