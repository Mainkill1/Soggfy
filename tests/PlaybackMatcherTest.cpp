#include "../SpotifyOggDumper/PlaybackMatcher.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

CompletedOgg Stream(uintptr_t context, double duration, uint8_t marker)
{
    CompletedOgg stream;
    stream.Context = context;
    stream.DurationSeconds = duration;
    stream.Bytes = { marker };
    return stream;
}

int main()
{
    PlaybackMatcher matcher(1.0, 8);

    assert(matcher.Announce({ "first", 120.0 }).empty());
    auto first = matcher.Complete(Stream(1, 120.4, 1));
    assert(first.size() == 1 && first[0].PlaybackId == "first" && first[0].Bytes[0] == 1);

    assert(matcher.Complete(Stream(2, 90.0, 2)).empty());
    auto delayed = matcher.Announce({ "delayed", 90.2 });
    assert(delayed.size() == 1 && delayed[0].PlaybackId == "delayed" && delayed[0].Bytes[0] == 2);

    assert(matcher.Announce({ "short", 60.0 }).empty());
    assert(matcher.Announce({ "long", 180.0 }).empty());
    auto longMatch = matcher.Complete(Stream(3, 180.3, 3));
    auto shortMatch = matcher.Complete(Stream(4, 59.8, 4));
    assert(longMatch.size() == 1 && longMatch[0].PlaybackId == "long");
    assert(shortMatch.size() == 1 && shortMatch[0].PlaybackId == "short");

    assert(matcher.Announce({ "mismatch", 30.0 }).empty());
    assert(matcher.Complete(Stream(5, 35.0, 5)).empty());
    assert(matcher.PendingPlaybackCount() == 1);
    assert(matcher.PendingStreamCount() == 1);

    PlaybackMatcher ordered(1.0, 8);
    assert(ordered.Announce({ "older", 100.0 }).empty());
    assert(ordered.Announce({ "newer", 100.0 }).empty());
    auto tie = ordered.Complete(Stream(6, 100.0, 6));
    assert(tie.size() == 1 && tie[0].PlaybackId == "older");

    PlaybackMatcher bounded(1.0, 2);
    assert(bounded.Announce({ "one", 10.0 }).empty());
    assert(bounded.Announce({ "two", 20.0 }).empty());
    assert(bounded.Announce({ "three", 30.0 }).empty());
    assert(bounded.PendingPlaybackCount() == 2);
    assert(bounded.Complete(Stream(7, 10.0, 7)).empty());

    assert(bounded.Cancel("two"));
    assert(!bounded.Cancel("missing"));
    assert(bounded.PendingPlaybackCount() == 1);
}
