#include "../SpotifyOggDumper/OggCapture.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

void Write32(std::vector<uint8_t>& data, size_t offset, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i) data[offset + i] = uint8_t(value >> (i * 8));
}

void Write64(std::vector<uint8_t>& data, size_t offset, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i) data[offset + i] = uint8_t(value >> (i * 8));
}

struct Page
{
    std::vector<uint8_t> Header = std::vector<uint8_t>(28, 0);
    std::vector<uint8_t> Body;

    Page(uint8_t flags, uint32_t number, int64_t granule, std::vector<uint8_t> body, uint32_t serial = 1) : Body(std::move(body))
    {
        std::memcpy(Header.data(), "OggS", 4);
        Header[5] = flags;
        Write64(Header, 6, uint64_t(granule));
        Write32(Header, 14, serial);
        Write32(Header, 18, number);
        Header[26] = 1;
        Header[27] = uint8_t(Body.size());
    }

    OggPageView View(uintptr_t context) const
    {
        return { context, Header.data(), Header.size(), Body.data(), Body.size() };
    }
};

std::vector<uint8_t> Identification(uint32_t sampleRate = 44100)
{
    std::vector<uint8_t> body(30, 0);
    body[0] = 1;
    std::memcpy(body.data() + 1, "vorbis", 6);
    body[11] = 2;
    Write32(body, 12, sampleRate);
    return body;
}

}

int main()
{
    OggStreamAssembler assembler(1024 * 1024);

    Page prefix(0, 99, 0, { 0x7f, 's', 'p', 'o', 't' });
    assert(!assembler.Push(prefix.View(1)));

    Page bos(2, 0, 0, Identification());
    assert(!assembler.Push(bos.View(1)));
    Page middle(0, 1, 22050, { 3, 'v', 'o', 'r', 'b', 'i', 's' });
    assert(!assembler.Push(middle.View(1)));
    Page eos(4, 2, 88200, { 0xaa, 0xbb });
    auto completed = assembler.Push(eos.View(1));
    assert(completed);
    assert(completed->Context == 1);
    assert(completed->PageCount == 3);
    assert(completed->Bytes.size() == bos.Header.size() + bos.Body.size() +
        middle.Header.size() + middle.Body.size() + eos.Header.size() + eos.Body.size());
    assert(completed->DurationSeconds == 2.0);

    Page wrongSequence(4, 4, 88200, { 0xcc });
    assert(!assembler.Push(bos.View(2)));
    assert(!assembler.Push(wrongSequence.View(2)));
    assert(!assembler.HasActiveStream(2));

    OggStreamAssembler small(50);
    assert(!small.Push(bos.View(3)));
    assert(!small.HasActiveStream(3));

    Page replacementBos(2, 0, 0, Identification(48000));
    assert(!assembler.Push(bos.View(4)));
    assert(!assembler.Push(replacementBos.View(4)));
    Page replacementEos(4, 1, 48000, { 0xdd });
    auto replacement = assembler.Push(replacementEos.View(4));
    assert(replacement && replacement->DurationSeconds == 1.0 && replacement->PageCount == 2);

    Page firstBos(2, 0, 0, Identification(), 10);
    Page secondBos(2, 0, 0, Identification(), 20);
    Page firstEos(4, 1, 44100, { 0x11 }, 10);
    Page secondEos(4, 1, 88200, { 0x22 }, 20);
    assert(!assembler.Push(firstBos.View(5)));
    assert(!assembler.Push(secondBos.View(5)));
    auto firstCompleted = assembler.Push(firstEos.View(5));
    auto secondCompleted = assembler.Push(secondEos.View(5));
    assert(firstCompleted && firstCompleted->DurationSeconds == 1.0);
    assert(secondCompleted && secondCompleted->DurationSeconds == 2.0);
}
