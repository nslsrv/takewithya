#include "output.h"
#include "printf.h"

#include <util/memory/tempbuf.h>
#include <util/generic/yexception.h>

size_t Printf(TOutputStream& out, const char* fmt, ...) {
    va_list lst;
    va_start(lst, fmt);
    size_t ret;

    try {
        ret = Printf(out, fmt, lst);
    } catch (...) {
        va_end(lst);

        throw;
    }

    va_end(lst);

    return ret;
}

static inline size_t TryPrintf(void* ptr, size_t len, TOutputStream& out, const char* fmt, va_list params) {
    va_list lst;
    va_copy(lst, params);
    const int ret = vsnprintf((char*)ptr, len, fmt, lst);
    va_end(lst);

    if (ret < 0) {
        return len;
    }

    if ((size_t)ret < len) {
        out.Write(ptr, (size_t)ret);
    }

    return (size_t)ret;
}

size_t Printf(TOutputStream& out, const char* fmt, va_list params) {
    size_t guess = 0;

    while (true) {
        TTempBuf tmp(guess);
        const size_t ret = TryPrintf(tmp.Data(), tmp.Size(), out, fmt, params);

        if (ret < tmp.Size()) {
            return ret;
        }

        guess = Max(tmp.Size() * 2, ret + 1);
    }

    return 0;
}
