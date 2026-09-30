#include "OggCapture.h"
#include "OggPageFilter.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace {

uint32_t Read32(const uint8_t* data)
{
    return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
        (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}

int64_t Read64(const uint8_t* data)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) value |= uint64_t(data[i]) << (i * 8);
    return static_cast<int64_t>(value);
}

bool IsValidPage(const OggPageView& page)
{
    if (!page.Context || !page.Header || !page.Body ||
        page.HeaderLength < 27 || page.HeaderLength > 282 ||
        page.BodyLength > 65025 || std::memcmp(page.Header, "OggS", 4) != 0 ||
        page.Header[4] != 0 || page.HeaderLength != size_t(27 + page.Header[26])) {
        return false;
    }

    size_t lacedBodyLength = 0;
    for (size_t i = 27; i < page.HeaderLength; ++i) lacedBodyLength += page.Header[i];
    return lacedBodyLength == page.BodyLength;
}

}

OggStreamAssembler::OggStreamAssembler(size_t maximumStreamBytes) :
    _maximumStreamBytes(maximumStreamBytes)
{
}

std::optional<CompletedOgg> OggStreamAssembler::Push(const OggPageView& page, OggCaptureEvent* event)
{
    if (event) *event = OggCaptureEvent::None;
    if (!IsValidPage(page)) {
        if (event) *event = OggCaptureEvent::InvalidPage;
        std::erase_if(_streams, [&](const auto& entry) { return entry.first.Context == page.Context; });
        return std::nullopt;
    }

    const bool isBos = (page.Header[5] & 0x02) != 0;
    const bool isEos = (page.Header[5] & 0x04) != 0;
    const uint32_t serial = Read32(page.Header + 14);
    const uint32_t pageNumber = Read32(page.Header + 18);
    const StreamKey key{ page.Context, serial };

    if (isBos) {
        _streams.erase(key);
        if (!IsVorbisIdentificationPage(true, page.Body, page.BodyLength) || page.BodyLength < 16) {
            if (event) *event = OggCaptureEvent::NonVorbisBos;
            return std::nullopt;
        }

        ActiveStream stream;
        stream.NextPageNumber = pageNumber;
        stream.SampleRate = Read32(page.Body + 12);
        if (!stream.SampleRate) return std::nullopt;
        _streams.emplace(key, std::move(stream));
        if (event) *event = OggCaptureEvent::Started;
    }

    auto found = _streams.find(key);
    if (found == _streams.end()) return std::nullopt;
    auto& stream = found->second;

    if (pageNumber != stream.NextPageNumber) {
        if (event) *event = OggCaptureEvent::SequenceMismatch;
        _streams.erase(found);
        return std::nullopt;
    }
    if (page.HeaderLength > _maximumStreamBytes ||
        page.BodyLength > _maximumStreamBytes - page.HeaderLength ||
        stream.Bytes.size() > _maximumStreamBytes - page.HeaderLength - page.BodyLength) {
        if (event) *event = OggCaptureEvent::SizeLimit;
        _streams.erase(found);
        return std::nullopt;
    }

    stream.Bytes.insert(stream.Bytes.end(), page.Header, page.Header + page.HeaderLength);
    stream.Bytes.insert(stream.Bytes.end(), page.Body, page.Body + page.BodyLength);
    ++stream.NextPageNumber;
    ++stream.PageCount;

    if (!isEos) return std::nullopt;

    const int64_t granule = Read64(page.Header + 6);
    if (granule < 0) {
        _streams.erase(found);
        return std::nullopt;
    }

    CompletedOgg completed;
    completed.Context = page.Context;
    completed.Serial = serial;
    completed.Bytes = std::move(stream.Bytes);
    completed.DurationSeconds = double(granule) / stream.SampleRate;
    completed.PageCount = stream.PageCount;
    _streams.erase(found);
    if (event) *event = OggCaptureEvent::Completed;
    return completed;
}

bool OggStreamAssembler::HasActiveStream(uintptr_t context) const
{
    return std::any_of(_streams.begin(), _streams.end(),
        [&](const auto& entry) { return entry.first.Context == context; });
}

void OggStreamAssembler::Clear()
{
    _streams.clear();
}
