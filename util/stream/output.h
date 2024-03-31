#pragma once

#include "labeled.h"

#include <util/generic/noncopyable.h>
#include <util/generic/string.h>
#include <util/generic/strbuf.h>
#include <util/generic/typetraits.h>

/**
 * @addtogroup Streams_Base
 * @{
 */

/**
 * Abstract output stream.
 */
class TOutputStream: public TNonCopyable {
public:
    /**
     * Data block for output.
     */
    struct TPart {
        inline TPart(const void* Buf, size_t Len) noexcept
            : buf(Buf)
            , len(Len)
        {
        }

        inline TPart(const TStringBuf s) noexcept
            : buf(~s)
            , len(+s)
        {
        }

        inline TPart() noexcept
            : buf(nullptr)
            , len(0)
        {
        }

        inline ~TPart() = default;

        static inline TPart CrLf() noexcept {
            return TPart("\r\n", 2);
        }

        const void* buf;
        size_t len;
    };

    TOutputStream() noexcept;
    virtual ~TOutputStream();

    TOutputStream(TOutputStream&&) noexcept {
    }

    TOutputStream& operator=(TOutputStream&&) noexcept {
        return *this;
    };

    /**
     * Writes into this stream.
     *
     * @param buf                       Data to write.
     * @param len                       Number of bytes to write.
     */
    inline void Write(const void* buf, size_t len) {
        if (len) {
            DoWrite(buf, len);
        }
    }

    /**
     * Writes a string into this stream.
     *
     * @param st                        String to write.
     */
    inline void Write(const TStringBuf st) {
        Write(~st, +st);
    }

    /**
     * Writes several data blocks into this stream.
     *
     * @param parts                     Pointer to the start of the data blocks
     *                                  array.
     * @param count                     Number of data blocks to write.
     */
    inline void Write(const TPart* parts, size_t count) {
        if (count > 1) {
            DoWriteV(parts, count);
        } else if (count) {
            DoWrite(parts->buf, parts->len);
        }
    }

    /**
     * Writes a single character into this stream.
     *
     * @param ch                        Character to write.
     */
    inline void Write(char ch) {
        Write(&ch, 1);
    }

    /**
     * Flushes this stream's buffer, if any.
     *
     * Note that this can also be done with a `Flush` manipulator:
     * @code
     * stream << "some string" << Flush;
     * @endcode
     */
    inline void Flush() {
        DoFlush();
    }

    /**
     * Flushes and closes this stream. No more data can be written into a stream
     * once it's closed.
     */
    inline void Finish() {
        DoFinish();
    }

protected:
    /**
     * Writes into this stream.
     *
     * @param buf                       Data to write.
     * @param len                       Number of bytes to write.
     * @throws yexception               If IO error occurs.
     */
    virtual void DoWrite(const void* buf, size_t len) = 0;

    /**
     * Writes several data blocks into this stream.
     *
     * @param parts                     Pointer to the start of the data blocks
     *                                  array.
     * @param count                     Number of data blocks to write.
     * @throws yexception               If IO error occurs.
     */
    virtual void DoWriteV(const TPart* parts, size_t count);

    /**
     * Flushes this stream's buffer, if any.
     *
     * @throws yexception               If IO error occurs.
     */
    virtual void DoFlush();

    /**
     * Flushes and closes this stream. No more data can be written into a stream
     * once it's closed.
     *
     * @throws yexception               If IO error occurs.
     */
    virtual void DoFinish();
};

/**
 * `operator<<` for `TOutputStream` by default delegates to this function.
 *
 * Note that while `operator<<` uses overloading (and thus argument-dependent
 * lookup), `Out` uses template specializations. This makes it possible to
 * have a single `Out` declaration, and then just provide specializations in
 * cpp files, letting the linker figure everything else out. This approach
 * reduces compilation times.
 *
 * However, if the flexibility of overload resolution is needed, then one should
 * just overload `operator<<`.
 *
 * @param out                           Output stream to write into.
 * @param value                         Value to write.
 */
template <class T>
void Out(TOutputStream& out, typename TTypeTraits<T>::TFuncParam value);

#define Y_DECLARE_OUT_SPEC(MODIF, T, stream, value) \
    template <>                                     \
    MODIF void Out<T>(TOutputStream & stream, TTypeTraits<T>::TFuncParam value)

template <>
inline void Out<const char*>(TOutputStream& o, const char* t) {
    if (t) {
        o.Write(t);
    } else {
        o.Write("(null)");
    }
}

template <>
void Out<const wchar16*>(TOutputStream& o, const wchar16* w);

using TStreamManipulator = void (*)(TOutputStream&);

static inline TOutputStream& operator<<(TOutputStream& o, TStreamManipulator m) {
    m(o);

    return o;
}

static inline TOutputStream& operator<<(TOutputStream& o, const char* t) {
    Out<const char*>(o, t);

    return o;
}

static inline TOutputStream& operator<<(TOutputStream& o, char* t) {
    Out<const char*>(o, t);

    return o;
}

template <class T>
static inline TOutputStream& operator<<(TOutputStream& o, const T& t) {
    Out<T>(o, t);

    return o;
}

static inline TOutputStream& operator<<(TOutputStream& o, const wchar16* t) {
    Out<const wchar16*>(o, t);
    return o;
}

static inline TOutputStream& operator<<(TOutputStream& o, wchar16* t) {
    Out<const wchar16*>(o, t);
    return o;
}

namespace NPrivate {
    TOutputStream& StdOutStream() noexcept;
    TOutputStream& StdErrStream() noexcept;
}

/**
 * Standard output stream.
 */
#define Cout (::NPrivate::StdOutStream())

/**
 * Standard error stream.
 */
#define Cerr (::NPrivate::StdErrStream())

/**
 * Standard log stream.
 */
#define Clog Cerr

/**
 * End-of-line output manipulator, basically the same as `std::endl`.
 */
static inline void Endl(TOutputStream& o) {
    (o << '\n').Flush();
}

/**
 * Flushing stream manipulator, basically the same as `std::flush`.
 */
static inline void Flush(TOutputStream& o) {
    o.Flush();
}

/*
 * Also see format.h for additional manipulators.
 */

#include "debug.h"

void RedirectStdioToAndroidLog(bool redirect);

/** @} */
