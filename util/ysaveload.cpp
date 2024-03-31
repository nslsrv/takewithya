#include "ysaveload.h"

#include <util/generic/buffer.h>

void TSerializer<TBuffer>::Save(TOutputStream* rh, const TBuffer& buf) {
    ::SaveSize(rh, buf.Size());
    ::SavePodArray(rh, buf.Data(), buf.Size());
}

void TSerializer<TBuffer>::Load(TInputStream* rh, TBuffer& buf) {
    const size_t s = ::LoadSize(rh);
    buf.Resize(s);
    ::LoadPodArray(rh, buf.Data(), buf.Size());
}
