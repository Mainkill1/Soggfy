#include "PlaybackMatcher.h"

#include <algorithm>
#include <cmath>
#include <limits>

PlaybackMatcher::PlaybackMatcher(double durationToleranceSeconds, size_t maximumPending) :
    _durationToleranceSeconds(durationToleranceSeconds),
    _maximumPending(maximumPending)
{
}

std::vector<MatchedOgg> PlaybackMatcher::Announce(PlaybackAnnouncement playback)
{
    if (playback.PlaybackId.empty() || playback.DurationSeconds <= 0) return {};

    auto duplicate = std::find_if(_playbacks.begin(), _playbacks.end(), [&](const auto& pending) {
        return pending.Value.PlaybackId == playback.PlaybackId;
    });
    if (duplicate == _playbacks.end()) {
        _playbacks.push_back({ std::move(playback), _nextOrdinal++ });
        while (_playbacks.size() > _maximumPending) _playbacks.pop_front();
    }
    return Match();
}

std::vector<MatchedOgg> PlaybackMatcher::Complete(CompletedOgg stream)
{
    if (stream.Bytes.empty() || stream.DurationSeconds <= 0) return {};
    _streams.push_back({ std::move(stream), _nextOrdinal++ });
    while (_streams.size() > _maximumPending) _streams.pop_front();
    return Match();
}

bool PlaybackMatcher::Cancel(const std::string& playbackId)
{
    auto found = std::find_if(_playbacks.begin(), _playbacks.end(), [&](const auto& pending) {
        return pending.Value.PlaybackId == playbackId;
    });
    if (found == _playbacks.end()) return false;
    _playbacks.erase(found);
    return true;
}

std::vector<MatchedOgg> PlaybackMatcher::Match()
{
    std::vector<MatchedOgg> matches;
    for (auto stream = _streams.begin(); stream != _streams.end();) {
        auto best = _playbacks.end();
        double bestDifference = std::numeric_limits<double>::max();
        for (auto playback = _playbacks.begin(); playback != _playbacks.end(); ++playback) {
            const double difference = std::fabs(playback->Value.DurationSeconds - stream->Value.DurationSeconds);
            if (difference <= _durationToleranceSeconds && difference < bestDifference) {
                best = playback;
                bestDifference = difference;
            }
        }

        if (best == _playbacks.end()) {
            ++stream;
            continue;
        }

        matches.push_back({ best->Value.PlaybackId, std::move(stream->Value.Bytes) });
        _playbacks.erase(best);
        stream = _streams.erase(stream);
    }
    return matches;
}

size_t PlaybackMatcher::PendingPlaybackCount() const
{
    return _playbacks.size();
}

size_t PlaybackMatcher::PendingStreamCount() const
{
    return _streams.size();
}

void PlaybackMatcher::Clear()
{
    _playbacks.clear();
    _streams.clear();
}
