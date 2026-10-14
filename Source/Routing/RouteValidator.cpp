#include "RouteValidator.h"
#include <algorithm>

namespace crowdmike::RouteValidator {
bool isLegalRoute(NodeClass s, NodeClass d) noexcept {
    using N = NodeClass;
    switch (s) {
        case N::RawInput:  return d == N::ProcInput || d == N::Recorder;
        case N::ProcInput: return d == N::Group || d == N::FxSend || d == N::Recorder || d == N::Output;
        case N::Group:     return d == N::FxSend || d == N::Master || d == N::Recorder || d == N::Output;
        case N::FxSend:    return d == N::FxReturn;
        case N::FxReturn:  return d == N::Master || d == N::Recorder || d == N::Output;
        case N::Playback:  return d == N::Master || d == N::Group || d == N::Recorder || d == N::Output;
        case N::Master:    return d == N::Recorder || d == N::Output;
        case N::Output:    return d == N::Recorder;
        case N::Recorder:  return false;
    }
    return false;
}

namespace {
std::vector<std::vector<int>> adjacency(int n, const std::vector<Edge>& edges, bool reverse) {
    std::vector<std::vector<int>> adj(static_cast<size_t>(n));
    for (const auto& e : edges)
        if (e.from >= 0 && e.from < n && e.to >= 0 && e.to < n)
            adj[static_cast<size_t>(reverse ? e.to : e.from)].push_back(reverse ? e.from : e.to);
    return adj;
}
}

bool hasCycle(int n, const std::vector<Edge>& edges) {
    const auto adj = adjacency(n, edges, false);
    std::vector<int> colour(static_cast<size_t>(n), 0);   // 0 unvisited, 1 on stack, 2 done
    std::vector<std::pair<int, size_t>> stack;
    for (int start = 0; start < n; ++start) {
        if (colour[static_cast<size_t>(start)] != 0) continue;
        stack.push_back({ start, 0 });
        colour[static_cast<size_t>(start)] = 1;
        while (!stack.empty()) {
            auto& [node, idx] = stack.back();
            const auto& next = adj[static_cast<size_t>(node)];
            if (idx < next.size()) {
                const int to = next[idx++];
                if (colour[static_cast<size_t>(to)] == 1) return true;
                if (colour[static_cast<size_t>(to)] == 0) { colour[static_cast<size_t>(to)] = 1; stack.push_back({ to, 0 }); }
            } else {
                colour[static_cast<size_t>(node)] = 2;
                stack.pop_back();
            }
        }
    }
    return false;
}

bool wouldCreateCycle(int n, const std::vector<Edge>& edges, Edge candidate) {
    if (candidate.from == candidate.to) return true;
    auto copy = edges;
    copy.push_back(candidate);
    return hasCycle(n, copy);
}

static std::vector<int> bfs(int n, const std::vector<Edge>& edges, int node, bool reverse) {
    std::vector<int> out;
    if (node < 0 || node >= n) return out;
    const auto adj = adjacency(n, edges, reverse);
    std::vector<char> seen(static_cast<size_t>(n), 0);
    std::vector<int> queue { node };
    seen[static_cast<size_t>(node)] = 1;
    for (size_t head = 0; head < queue.size(); ++head) {
        out.push_back(queue[head]);
        for (int to : adj[static_cast<size_t>(queue[head])])
            if (!seen[static_cast<size_t>(to)]) { seen[static_cast<size_t>(to)] = 1; queue.push_back(to); }
    }
    return out;
}
std::vector<int> reachableFrom(int n, const std::vector<Edge>& e, int node) { return bfs(n, e, node, false); }
std::vector<int> reachingTo(int n, const std::vector<Edge>& e, int node) { return bfs(n, e, node, true); }

std::vector<Edge> edgesFromState(const RoutingState& s, bool groupsFeedFx) {
    std::vector<Edge> edges;
    constexpr float eps = 1.0e-4f;
    for (int o = 0; o < kMaxOutputs; ++o)
        for (int i = 0; i < kMaxInputs; ++i)
            if (std::abs(s.getDirect(i, o)) > eps) edges.push_back({ nodeInput(i), nodeOutput(o) });
    for (int g = 0; g < kMaxGroups; ++g)
        for (int i = 0; i < kMaxInputs; ++i)
            if (s.getAssign(i, g) > eps) edges.push_back({ nodeInput(i), nodeGroup(g) });
    for (int o = 0; o < kMaxOutputs; ++o)
        for (int r = 0; r < kBusSrcCount; ++r) {
            if (std::abs(s.getBus(r, o)) <= eps) continue;
            int from;
            if (r < kBusSrcFxBase) from = nodeGroup(r / 2);
            else if (r < kBusSrcPlayback) from = nodeFxReturn((r - kBusSrcFxBase) / 2);
            else if (r < kBusSrcMaster) from = nodePlayback();
            else from = nodeMaster();
            edges.push_back({ from, nodeOutput(o) });
        }
    for (int g = 0; g < kMaxGroups; ++g) edges.push_back({ nodeGroup(g), nodeMaster() });
    for (int f = 0; f < kNumFx; ++f) {
        if (groupsFeedFx) for (int g = 0; g < kMaxGroups; ++g) edges.push_back({ nodeGroup(g), nodeFxSend(f) });
        edges.push_back({ nodeFxSend(f), nodeFxReturn(f) });
        edges.push_back({ nodeFxReturn(f), nodeMaster() });
    }
    return edges;
}

std::vector<RouteIssue> validate(const RoutingState& s, int activeInputs, int activeOutputs) {
    std::vector<RouteIssue> issues;
    const auto edges = edgesFromState(s);
    if (hasCycle(nodeCount(), edges)) issues.push_back({ RouteIssue::Error, "Routing contains a signal cycle" });

    constexpr float eps = 1.0e-4f;
    for (int g = 0; g < kMaxGroups; ++g) {
        int members = 0;
        for (int i = 0; i < activeInputs; ++i) members += s.getAssign(i, g) > eps ? 1 : 0;
        if (members == 0) continue;
        const auto reach = reachableFrom(nodeCount(), edges, nodeGroup(g));
        bool reachesOutput = false;
        for (int o = 0; o < activeOutputs; ++o)
            reachesOutput = reachesOutput || std::find(reach.begin(), reach.end(), nodeOutput(o)) != reach.end();
        if (!reachesOutput)
            issues.push_back({ RouteIssue::Warning, "Group " + juce::String(g + 1) + " has inputs but reaches no output" });
    }
    for (int o = 0; o < activeOutputs; ++o)
        for (int i = 0; i < activeInputs; ++i)
            if (std::abs(s.getDirect(i, o)) > 0.5f)
                issues.push_back({ RouteIssue::Info, "Input " + juce::String(i + 1) + " is routed directly to output "
                                                         + juce::String(o + 1) + " (live microphone to loudspeaker: check feedback risk)" });
    return issues;
}
}
