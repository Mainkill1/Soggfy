#include "../SpotifyOggDumper/OggPageFilter.h"

#include <cassert>
#include <cstdint>

int main()
{
    const uint8_t vorbis[] = { 1, 'v', 'o', 'r', 'b', 'i', 's', 0 };
    const uint8_t custom[] = { 'S', 'p', 'o', 't', 'i', 'f', 'y' };

    assert(IsVorbisIdentificationPage(true, vorbis, sizeof(vorbis)));
    assert(!IsVorbisIdentificationPage(false, vorbis, sizeof(vorbis)));
    assert(!IsVorbisIdentificationPage(true, custom, sizeof(custom)));
    assert(!IsVorbisIdentificationPage(true, vorbis, 6));
}
