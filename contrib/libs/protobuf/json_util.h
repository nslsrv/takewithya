#pragma once

#include <contrib/libs/protobuf/stubs/common.h>

namespace google {
namespace protobuf {
namespace io {

void PrintJSONString(TOutputStream& stream, const TProtoStringType& string);

template<class T>
struct TAsJSON {
public:
    TAsJSON(const T& t)
        : T_(t)
    {
    }

    const T& T_;
};

template<class T>
inline TOutputStream& operator <<(TOutputStream& stream, const TAsJSON<T>& protoAsJSON) {
    protoAsJSON.T_.PrintJSON(stream);
    return stream;
};

}
}
}
