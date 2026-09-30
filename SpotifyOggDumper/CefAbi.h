#pragma once

#include <cstddef>

namespace CefAbi {

struct Base
{
    size_t Size;
    void (*AddRef)(Base*);
    int (*Release)(Base*);
    int (*HasOneRef)(Base*);
    int (*HasAtLeastOneRef)(Base*);
};

struct String
{
    wchar_t* Value;
    size_t Length;
    void (*Destructor)(wchar_t*);
};

template<size_t MethodCount>
struct Object
{
    Base BaseObject;
    void* Methods[MethodCount];
};

inline bool IsSupportedVersion(int major, int minor, int patch)
{
    return major == 146 && minor == 0 && patch == 10;
}

}
