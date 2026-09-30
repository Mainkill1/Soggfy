#pragma once

#include "OggCapture.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct PlaybackAnnouncement
{
    std::string PlaybackId;
    double DurationSeconds = 0;
};

struct MatchedOgg
{
    std::string PlaybackId;
    std::vector<uint8_t> Bytes;
};

class PlaybackMatcher
{
public:
    PlaybackMatcher(double durationToleranceSeconds, size_t maximumPending);

    std::vector<MatchedOgg> Announce(PlaybackAnnouncement playback);
    std::vector<MatchedOgg> Complete(CompletedOgg stream);
    bool Cancel(const std::string& playbackId);
    size_t PendingPlaybackCount() const;
    size_t PendingStreamCount() const;
    void Clear();

private:
    std::vector<MatchedOgg> Match();

    double _durationToleranceSeconds;
    size_t _maximumPending;
    uint64_t _nextOrdinal = 0;

    struct PendingPlayback
    {
        PlaybackAnnouncement Value;
        uint64_t Ordinal;
    };
    struct PendingStream
    {
        CompletedOgg Value;
        uint64_t Ordinal;
    };

    std::deque<PendingPlayback> _playbacks;
    std::deque<PendingStream> _streams;
};
