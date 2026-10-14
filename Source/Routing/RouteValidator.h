#pragma once
// Route legality, cycle detection, path tracing and structural warnings (ROUTING_SPEC section 1, GUI_SPEC section 7).
// The engine topology is feed-forward by construction (input -> group -> fx -> master -> output); this validator keeps it
// that way for any future user-editable graph and powers the Matrix page (blocked cells, path highlight, warnings).
#include <juce_core/juce_core.h>
#include <vector>
#include "RoutingState.h"

namespace crowdmike {
enum class NodeClass { RawInput, ProcInput, Group, FxSend, FxReturn, Playback, Master, Recorder, Output };

struct Edge { int from = 0, to = 0; };

struct RouteIssue {
    enum Severity { Info, Warning, Error } severity = Info;
    juce::String message;
};

namespace RouteValidator {
// True when a route from `src` class to `dst` class is part of the legal feed-forward topology.
bool isLegalRoute(NodeClass src, NodeClass dst) noexcept;
bool hasCycle(int nodeCount, const std::vector<Edge>& edges);
bool wouldCreateCycle(int nodeCount, const std::vector<Edge>& edges, Edge candidate);
// Nodes reachable from / that can reach `node` (including the node itself); for hover path highlighting.
std::vector<int> reachableFrom(int nodeCount, const std::vector<Edge>& edges, int node);
std::vector<int> reachingTo(int nodeCount, const std::vector<Edge>& edges, int node);

// Unified node numbering used by edgesFromState().
constexpr int nodeInput(int i) { return i; }
constexpr int nodeGroup(int g) { return kMaxInputs + g; }
constexpr int nodeFxSend(int f) { return kMaxInputs + kMaxGroups + f; }
constexpr int nodeFxReturn(int f) { return kMaxInputs + kMaxGroups + kNumFx + f; }
constexpr int nodePlayback() { return kMaxInputs + kMaxGroups + 2 * kNumFx; }
constexpr int nodeMaster() { return nodePlayback() + 1; }
constexpr int nodeOutput(int o) { return nodeMaster() + 1 + o; }
constexpr int nodeCount() { return nodeOutput(kMaxOutputs); }

// Edges implied by the three routing matrices plus the fixed FX topology (group -> fx -> return -> master).
std::vector<Edge> edgesFromState(const RoutingState& state, bool groupsFeedFx = true);
std::vector<RouteIssue> validate(const RoutingState& state, int activeInputs, int activeOutputs);
}
}
