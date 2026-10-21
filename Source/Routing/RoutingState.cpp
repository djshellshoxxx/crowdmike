#include "RoutingState.h"
#include "DSP/RoutingMatrix.h"

namespace crowdmike {
namespace {
template <typename Array>
juce::String pack(const Array& a) {
    juce::StringArray parts;
    for (const auto& v : a) parts.add(juce::String(v.load(std::memory_order_relaxed), 4));
    return parts.joinIntoString(" ");
}
template <typename Array>
bool unpack(Array& a, const juce::String& text, float lo, float hi) {
    const auto parts = juce::StringArray::fromTokens(text, " ", "");
    if (static_cast<size_t>(parts.size()) != a.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const double v = parts[static_cast<int>(i)].getDoubleValue();
        a[i].store(std::isfinite(v) ? juce::jlimit(lo, hi, static_cast<float>(v)) : 0.0f, std::memory_order_relaxed);
    }
    return true;
}
}

void RoutingState::clearAll() noexcept {
    for (auto& v : direct) v.store(0.0f, std::memory_order_relaxed);
    for (auto& v : assign) v.store(0.0f, std::memory_order_relaxed);
    for (auto& v : bus) v.store(0.0f, std::memory_order_relaxed);
    touch();
}

void RoutingState::resetDirectDefault(int inputChannels, int outputChannels) noexcept {
    RoutingMatrix defaults;
    defaults.resetToIdentity(inputChannels, outputChannels);
    for (int out = 0; out < kOut; ++out)
        for (int in = 0; in < kIn; ++in)
            direct[di(in, out)].store(in < inputChannels && out < outputChannels ? defaults.getRouteGain(in, out) : 0.0f,
                                      std::memory_order_relaxed);
    touch();
}

juce::ValueTree RoutingState::toValueTree() const {
    juce::ValueTree t("Routing");
    t.setProperty("version", 1, nullptr);
    t.setProperty("direct", pack(direct), nullptr);
    t.setProperty("assign", pack(assign), nullptr);
    t.setProperty("bus", pack(bus), nullptr);
    t.setProperty("savedDirect", hasSavedDirect(), nullptr);
    return t;
}

bool RoutingState::fromValueTree(const juce::ValueTree& t) {
    if (!t.isValid() || t.getType().toString() != "Routing" || static_cast<int>(t["version"]) != 1) return false;
    // Validate sizes first so a malformed tree can never leave a half-applied matrix.
    const auto count = [](const juce::String& s) { return juce::StringArray::fromTokens(s, " ", "").size(); };
    if (count(t["direct"].toString()) != kIn * kOut || count(t["assign"].toString()) != kIn * kGroups
        || count(t["bus"].toString()) != kBus * kOut)
        return false;
    unpack(direct, t["direct"].toString(), -2.0f, 2.0f);
    unpack(assign, t["assign"].toString(), 0.0f, 2.0f);
    unpack(bus, t["bus"].toString(), -2.0f, 2.0f);
    setHasSavedDirect(static_cast<bool>(t["savedDirect"]));
    touch();
    return true;
}

juce::String RoutingState::busRowName(int row) {
    if (row >= kBusSrcGroupBase && row < kBusSrcFxBase)
        return "Group " + juce::String((row - kBusSrcGroupBase) / 2 + 1) + ((row & 1) ? " R" : " L");
    if (row >= kBusSrcFxBase && row < kBusSrcPlayback) {
        static const char* names[] = { "Delay", "Echo", "Tape", "Reverse", "Noise" };
        const int rel = row - kBusSrcFxBase;
        return juce::String(names[rel / 2]) + ((rel & 1) ? " R" : " L");
    }
    if (row >= kBusSrcPlayback && row < kBusSrcMaster) return (row & 1) ? "Playback R" : "Playback L";
    if (row >= kBusSrcMaster && row < kBusSrcCount) return (row & 1) ? "Master R" : "Master L";
    return {};
}
}
