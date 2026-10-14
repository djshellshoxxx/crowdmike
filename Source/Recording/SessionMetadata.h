#pragma once
// Recording session sidecar JSON (RECORDING_SPEC section 13): data types + serialisation.
// Never used on the audio thread.
#include <juce_core/juce_core.h>
#include <vector>

namespace crowdmike::rec {

enum class MarkerType { BigCheer, Chant, Applause, PerformerMoment, Problem, Custom };
juce::String markerTypeToString(MarkerType);
MarkerType markerTypeFromString(const juce::String&);   // unknown -> Custom

struct Marker {
    juce::int64 frame = 0;      // RecorderEngine::getMarkers(): absolute engine frame. SessionInfo: frame relative to file start.
    double seconds = 0.0;       // SessionInfo only: frame / sampleRate
    juce::String label, scene;
    MarkerType type = MarkerType::Custom;
    juce::Time wallClock;
};

struct DropoutEvent {
    juce::int64 frame = 0;      // approximate; relative to file start in SessionInfo, absolute engine frame otherwise
    int track = -1;
    int frames = 0;             // number of frames lost (written as silence)
};

struct SessionInfo {
    struct Track { int track = 0; int sourceId = 0; juce::String name; int numChannels = 1; };
    struct FileEntry {
        juce::String fileName;  // file name only; resolve with `file`
        juce::File file;        // resolved next to the JSON when read back
        int track = 0, sourceId = 0, numChannels = 1, part = 1;
        juce::String trackName;
        juce::int64 startFrame = 0;   // relative to the session start (pre-roll included)
        juce::int64 numFrames = 0;
    };
    juce::String appVersion, startTimeIso, scene, show, venue, fileFormat;
    double sampleRate = 0.0;
    int take = 0;
    juce::int64 startFrame = 0;     // absolute engine frame of the first recorded frame
    juce::int64 preRollFrames = 0;
    juce::int64 totalFrames = 0;
    std::vector<Track> tracks;
    std::vector<FileEntry> files;
    std::vector<Marker> markers;
    std::vector<DropoutEvent> dropouts;
    juce::var routeSnapshot;

    /** All files of one track, ordered by part. */
    std::vector<FileEntry> filesForTrack(int track) const;
};

class SessionMetadata {
public:
    static juce::var toVar(const SessionInfo&);
    /** Atomic write (temp file + rename). */
    static bool write(const juce::File& json, const SessionInfo&, juce::String& error);
    static bool read(const juce::File& json, SessionInfo& out);
};

} // namespace crowdmike::rec
