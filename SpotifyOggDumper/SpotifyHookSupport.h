#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

inline constexpr std::array<uint8_t, 32> SpotifyOggParserSignature = {
    0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c,
    0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x83, 0xec, 0x40, 0x48, 0x63, 0x79, 0x10
};

inline bool IsSupportedSpotifyParser(const uint8_t* code, size_t length)
{
    return code && length >= SpotifyOggParserSignature.size() &&
        std::equal(SpotifyOggParserSignature.begin(), SpotifyOggParserSignature.end(), code);
}

inline const uint8_t* FindUniqueSpotifyParser(const uint8_t* code, size_t length)
{
    if (!code || length < SpotifyOggParserSignature.size()) return nullptr;

    const auto begin = code;
    const auto end = code + length;
    const auto first = std::search(begin, end,
        SpotifyOggParserSignature.begin(), SpotifyOggParserSignature.end());
    if (first == end) return nullptr;

    const auto next = std::search(first + 1, end,
        SpotifyOggParserSignature.begin(), SpotifyOggParserSignature.end());
    return next == end ? first : nullptr;
}
