#include "../SpotifyOggDumper/SpotifyHookSupport.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    std::array<uint8_t, 32> code = {
        0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c,
        0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
        0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x83, 0xec, 0x40, 0x48, 0x63, 0x79, 0x10
    };

    assert(IsSupportedSpotifyParser(code.data(), code.size()));
    code[4] ^= 0xff;
    assert(!IsSupportedSpotifyParser(code.data(), code.size()));
    assert(!IsSupportedSpotifyParser(nullptr, code.size()));
    assert(!IsSupportedSpotifyParser(code.data(), 15));

    code[4] ^= 0xff;
    std::vector<uint8_t> image(160, 0xcc);
    std::copy(code.begin(), code.end(), image.begin() + 41);
    assert(FindUniqueSpotifyParser(image.data(), image.size()) == image.data() + 41);

    std::copy(image.begin() + 41, image.begin() + 73, image.begin() + 100);
    assert(FindUniqueSpotifyParser(image.data(), image.size()) == nullptr);
    assert(FindUniqueSpotifyParser(nullptr, image.size()) == nullptr);
}
