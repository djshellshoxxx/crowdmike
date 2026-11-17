#include "RecTestUtil.h"
#include "Recording/SessionMetadata.h"
#include "Recording/SoundcheckPlayer.h"
#include <limits>

using namespace crowdmike::rec;

namespace {
bool readSessionFixture(const juce::File& json, const SessionInfo& info) {
    juce::String error;
    CM_EXPECT(SessionMetadata::write(json, info, error));
    SessionInfo decoded;
    return SessionMetadata::read(json, decoded);
}
}

CM_TEST(SessionMetadata_RejectsNegativeTimelineBounds) {
    rectest::TempDir tmp("session-safety");
    const auto json = tmp.dir.getChildFile("session.json");
    for (int field = 0; field < 3; ++field) {
        SessionInfo info;
        info.sampleRate = 48000.0;
        if (field == 0) info.startFrame = -1;
        if (field == 1) info.preRollFrames = -1;
        if (field == 2) info.totalFrames = -1;
        CM_EXPECT(! readSessionFixture(json, info));
    }
}

CM_TEST(SessionMetadata_RejectsNegativeOrOverflowingFileExtents) {
    rectest::TempDir tmp("session-safety");
    const auto json = tmp.dir.getChildFile("session.json");
    for (const auto [start, length] : {
             std::pair<juce::int64, juce::int64> { -1024, 192000 },
             { 0, -1 },
             { std::numeric_limits<juce::int64>::max() - 1023, 1024 } }) {
        SessionInfo info;
        info.sampleRate = 48000.0;
        SessionInfo::FileEntry part;
        part.fileName = "missing.wav";
        part.startFrame = start;
        part.numFrames = length;
        info.files.push_back(part);
        CM_EXPECT(! readSessionFixture(json, info));
    }
}

CM_TEST(SessionMetadata_PreservesLongNonnegativeFileExtents) {
    rectest::TempDir tmp("session-safety");
    SessionInfo info;
    info.sampleRate = 48000.0;
    info.startFrame = 1ll << 42;
    info.totalFrames = (1ll << 40) + 48000;
    info.preRollFrames = 96000;
    SessionInfo::FileEntry part;
    part.fileName = "long-part.wav";
    part.startFrame = 1ll << 40;
    part.numFrames = 48000;
    info.files.push_back(part);
    const auto json = tmp.dir.getChildFile("session.json");
    juce::String error;
    CM_EXPECT(SessionMetadata::write(json, info, error));
    SessionInfo decoded;
    CM_EXPECT(SessionMetadata::read(json, decoded));
    CM_EXPECT(decoded.startFrame == (1ll << 42));
    CM_EXPECT(decoded.totalFrames == (1ll << 40) + 48000);
    CM_EXPECT(decoded.files.size() == 1);
    if (! decoded.files.empty()) {
        CM_EXPECT(decoded.files[0].startFrame == (1ll << 40));
        CM_EXPECT(decoded.files[0].numFrames == 48000);
    }
}

CM_TEST(Soundcheck_RejectsNegativeSessionPartBeforeLoading) {
    rectest::TempDir tmp("session-safety");
    SessionInfo info;
    info.sampleRate = 48000.0;
    info.tracks.push_back({ 0, 0, "Raw", 1 });
    SessionInfo::FileEntry part;
    part.fileName = "missing.wav";
    part.startFrame = -1024;
    part.numFrames = 192000;
    info.files.push_back(part);
    const auto json = tmp.dir.getChildFile("session.json");
    juce::String error;
    CM_EXPECT(SessionMetadata::write(json, info, error));
    SoundcheckPlayer player;
    player.prepare(48000.0, 256);
    CM_EXPECT(! player.loadSession(json));
    CM_EXPECT(! player.getLoadState().loaded);
}
