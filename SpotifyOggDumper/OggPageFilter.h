#pragma once

#include <cstddef>
#include <cstdint>

inline bool IsVorbisIdentificationPage(bool bos, const uint8_t* body, size_t length)
{
    static constexpr uint8_t signature[] = { 1, 'v', 'o', 'r', 'b', 'i', 's' };
    if (!bos || !body || length < sizeof(signature)) return false;

    for (size_t i = 0; i < sizeof(signature); ++i) {
        if (body[i] != signature[i]) return false;
    }
    return true;
}
