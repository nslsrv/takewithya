#include "null.h"
#include "debug.h"

#include <util/string/cast.h>
#include <util/generic/singleton.h>
#include <util/generic/yexception.h>

#include <cstdio>
#include <cstdlib>

void TDebugOutput::DoWrite(const void* buf, size_t len) {
    if (len != fwrite(buf, 1, len, stderr)) {
        ythrow yexception() << "write failed(" << LastSystemErrorText() << ")";
    }
}

namespace {
    struct TDbgSelector {
        inline TDbgSelector() {
            char* dbg = getenv("DBGOUT");
            if (dbg) {
                Out = &Cerr;
                try {
                    Level = FromString(dbg);
                } catch (const yexception&) {
                    Level = 0;
                }
            } else {
                Out = &Cnull;
                Level = 0;
            }
        }

        TOutputStream* Out;
        int Level;
    };
}

TOutputStream& StdDbgStream() noexcept {
    return *(Singleton<TDbgSelector>()->Out);
}

int StdDbgLevel() noexcept {
    return Singleton<TDbgSelector>()->Level;
}
