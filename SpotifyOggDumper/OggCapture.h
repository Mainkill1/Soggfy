#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

struct OggPageView
{
    uintptr_t Context;
    const uint8_t* Header;
    size_t HeaderLength;
    const uint8_t* Body;
    size_t BodyLength;
};

struct CompletedOgg
{
    uintptr_t Context = 0;
    uint32_t Serial = 0;
    std::vector<uint8_t> Bytes;
    double DurationSeconds = 0;
    size_t PageCount = 0;
};

enum class OggCaptureEvent
{
    None,
    InvalidPage,
    NonVorbisBos,
    Started,
    SequenceMismatch,
    SizeLimit,
    Completed
};

class OggStreamAssembler
{
public:
    explicit OggStreamAssembler(size_t maximumStreamBytes);

    std::optional<CompletedOgg> Push(const OggPageView& page, OggCaptureEvent* event = nullptr);
    bool HasActiveStream(uintptr_t context) const;
    void Clear();

private:
    struct StreamKey
    {
        uintptr_t Context;
        uint32_t Serial;

        bool operator==(const StreamKey&) const = default;
    };

    struct StreamKeyHash
    {
        size_t operator()(const StreamKey& key) const
        {
            return std::hash<uintptr_t>{}(key.Context) ^ (std::hash<uint32_t>{}(key.Serial) << 1);
        }
    };

    struct ActiveStream
    {
        std::vector<uint8_t> Bytes;
        uint32_t NextPageNumber = 0;
        uint32_t SampleRate = 0;
        size_t PageCount = 0;
    };

    size_t _maximumStreamBytes;
    std::unordered_map<StreamKey, ActiveStream, StreamKeyHash> _streams;
};
