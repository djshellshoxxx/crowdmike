#pragma once
// Message-thread-owned, non-parameter show state: port names/zones, macros, MIDI bindings, OSC and recorder settings,
// locked-parameter list. Fully serialisable (STATE section 10). Audio-visible pieces are published through
// ParamBank::publishMacros() / RoutingState, never read directly by the audio thread.
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <vector>
#include "Core/Limits.h"
#include "Core/ParamBank.h"

namespace crowdmike {
struct PortMeta {
    juce::String name, label, zone;
};

enum class MidiMsgType : int { CC = 0, NoteOnOff = 1, PitchBend = 2 };
enum class BindMode : int { Absolute = 0, Toggle = 1, Momentary = 2, Trigger = 3 };
enum class BindTarget : int {
    Param = 0, SceneSelect, SceneNext, ScenePrev, RecordStart, RecordStop, RecordToggle, RecordMarker, SplitFile,
    ClipTrigger, ClipStop, StopAllClips, Panic, MacroValue
};

struct ControlBinding {   // one MIDI or OSC-independent binding record
    MidiMsgType type = MidiMsgType::CC;
    int channel = 0;          // 0 = omni, 1..16
    int number = 0;           // CC number / note number (ignored for pitch bend)
    BindTarget target = BindTarget::Param;
    int targetIndex = 0;      // global param index / scene / clip slot / macro
    BindMode mode = BindMode::Absolute;
    float min01 = 0.0f, max01 = 1.0f;   // normalised range for Absolute mode
};

struct OscSettings {
    bool enabled = false;
    int port = 9000;
    bool localhostOnly = true;
    juce::String bindAddress;   // empty = all interfaces (or localhost when localhostOnly)
};

struct RecTrackSetting {
    int sourceId = -1;          // -1 = unused slot; see Engine/TapMap.h
    int numChannels = 1;
    bool armed = false;
    bool muted = false;
    bool monitor = false;
    juce::String name;
};

struct RecorderSettings {
    juce::String folder;
    int format = 1;             // 0 = WAV 16-bit, 1 = WAV 24-bit, 2 = WAV 32-bit float, 3 = FLAC 24-bit
    int preRollSeconds = 0;
    int postRollSeconds = 0;
    double maxFileSeconds = 0.0;
    double maxFileMegabytes = 0.0;
    juce::String pattern = "{date}_{time}_{scene}_{source}";
    juce::String show, venue;
    std::array<RecTrackSetting, kMaxRecordTracks> tracks {};
    // ---- appended (RECORDING_SPEC 6, 8, 14); defaults reproduce the behaviour of older shows
    int monitorPair = -1;               // cue output pair (2k, 2k+1) fed by the MON tracks, -1 = monitor off
    float monitorGainDb = 0.0f;         // cue level, -60..+12 dB
    bool autoRecordOnTransport = false; // start / stop takes with the host transport
    bool exportMarkers = false;         // write marker CSV + Audacity labels next to each session file
    int lowDiskWarnMB = 500;            // warn below this much free space (0 = off)
    int lowDiskStopMB = 100;            // end the take cleanly below this (0 = off)
};

struct UiSettings {
    bool highContrast = false;
    bool compact = false;
    bool tooltips = true;
    float scale = 1.0f;
    float recallRampMs = 60.0f;
    // FX page display order of the five send effects (a permutation of 0..kNumFx-1). Display only: DSP topology is fixed.
    std::array<int, kNumFx> fxOrder { 0, 1, 2, 3, 4 };
    unsigned recallScope = 0xFFu;   // SceneScope bits used by operator recalls (GUI / keyboard)
};

class ShowModel {
public:
    ShowModel();

    PortMeta& input(int i) { return inputs[static_cast<size_t>(i)]; }
    PortMeta& group(int g) { return groups[static_cast<size_t>(g)]; }
    PortMeta& output(int o) { return outputs[static_cast<size_t>(o)]; }
    const PortMeta& input(int i) const { return inputs[static_cast<size_t>(i)]; }
    const PortMeta& group(int g) const { return groups[static_cast<size_t>(g)]; }
    const PortMeta& output(int o) const { return outputs[static_cast<size_t>(o)]; }
    // Stable, rename-proof port identifiers ("in-1", "grp-3", "out-2").
    static juce::String inputId(int i) { return "in-" + juce::String(i + 1); }
    static juce::String groupId(int g) { return "grp-" + juce::String(g + 1); }
    static juce::String outputId(int o) { return "out-" + juce::String(o + 1); }
    juce::String inputDisplayName(int i) const { return inputs[static_cast<size_t>(i)].name.isNotEmpty() ? inputs[static_cast<size_t>(i)].name : "Input " + juce::String(i + 1); }
    juce::String groupDisplayName(int g) const { return groups[static_cast<size_t>(g)].name.isNotEmpty() ? groups[static_cast<size_t>(g)].name : "Group " + juce::String(g + 1); }
    juce::String outputDisplayName(int o) const { return outputs[static_cast<size_t>(o)].name.isNotEmpty() ? outputs[static_cast<size_t>(o)].name : "Out " + juce::String(o + 1); }

    MacroSet macros;
    std::array<juce::String, kNumMacros> macroNames;
    std::vector<ControlBinding> bindings;
    OscSettings osc;
    RecorderSettings recorder;
    UiSettings ui;
    std::vector<int> lockedParams;      // global ParamBank indices excluded from scene recall
    juce::ValueTree playback { "Playback" };   // opaque clip/bed/soundcheck references (owned by the recording layer)

    bool isLocked(int paramIndex) const;
    void setLocked(int paramIndex, bool locked);

    juce::ValueTree toValueTree() const;
    bool fromValueTree(const juce::ValueTree&);   // false => untouched (invalid)
    void resetToDefaults();

private:
    std::array<PortMeta, kMaxInputs> inputs;
    std::array<PortMeta, kMaxGroups> groups;
    std::array<PortMeta, kMaxOutputs> outputs;
};
}
