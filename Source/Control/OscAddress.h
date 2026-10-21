#pragma once
// Pure OSC address -> action resolver (CONTROL_SPEC section 6). No sockets here so it is fully unit-testable.
//   /crowdmike/master/level <dB>            /crowdmike/input/1/mute <0|1>        /crowdmike/group/2/trim <dB>
//   /crowdmike/output/3/delay <ms>          /crowdmike/fx/reverse/trigger         /crowdmike/fx/delay/feedback <0..0.95>
//   /crowdmike/scene/3/select               /crowdmike/scene/next|prev
//   /crowdmike/record/start|stop|toggle|marker|split
//   /crowdmike/clip/4/trigger|stop          /crowdmike/clips/stop
//   /crowdmike/macro/2 <0..1>               /crowdmike/panic [0|1]
//   /crowdmike/scene/scope <mask 1..255>    /crowdmike/scene/scope/<inputs|groups|fx|master|outputs|routing|names|macros|all> [0|1]
//   /crowdmike/tempo/tap|resync              /crowdmike/tempo/bpm|source|quantise|nudge|autoSource|barBeats <value>
//   /crowdmike/param/<full.param.id> <actual value>      /crowdmike/paramnorm/<full.param.id> <0..1>
#include <juce_core/juce_core.h>
#include <vector>

namespace crowdmike {
struct OscAction {
    enum class Kind { None, SetParam, SetParamNorm, Pulse, SceneSelect, SceneNext, ScenePrev, RecordStart, RecordStop,
                      RecordToggle, RecordMarker, RecordSplit, ClipTrigger, ClipStop, StopAllClips, Panic, Macro,
                      TempoTap, TempoResync, SceneScopeSet, SceneScopeBit } kind = Kind::None;
    int index = -1;          // global param index / scene / clip / macro / scope mask or bit
    bool hasValue = false;
    float value = 0.0f;
    juce::String error;      // set when the address was recognised but invalid
};

OscAction resolveOsc(const juce::String& address, const std::vector<float>& args);
// Friendly alias for a parameter suffix ("level" -> "levelDb", "trim" -> "trimDb", ...); returns the input when unaliased.
juce::String oscAliasSuffix(const juce::String& moduleKey, const juce::String& name);
}
