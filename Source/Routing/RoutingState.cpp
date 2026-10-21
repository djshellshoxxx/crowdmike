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
// Bus matrix persistence. The in-memory matrix is output-major with stride kBusSrcCount, but the saved form keeps the
// first-release layout so older states (and older builds reading newer states) stay valid: property "bus" holds the
// legacy rows 0..kBusSrcLegacyCount-1 (output-major, stride 30) and the rows appended later go to "busFx2"
// (output-major, stride kBusSrcCount - kBusSrcLegacyCount).
template <typename Array>
juce::String packBusRows(const Array& a, int rowBegin, int rowEnd) {
    juce::StringArray parts;
    for (int o = 0; o < kMaxOutputs; ++o)
        for (int r = rowBegin; r < rowEnd; ++r)
            parts.add(juce::String(a[static_cast<size_t>(o * kBusSrcCount + r)].load(std::memory_order_relaxed), 4));
    return parts.joinIntoString(" ");
}
template <typename Array>
void unpackBusRows(Array& a, const juce::String& text, int rowBegin, int rowEnd) {
    const auto parts = juce::StringArray::fromTokens(text, " ", "");
    int k = 0;
    for (int o = 0; o < kMaxOutputs; ++o)
        for (int r = rowBegin; r < rowEnd; ++r, ++k) {
            const double v = k < parts.size() ? parts[k].getDoubleValue() : 0.0;
            a[static_cast<size_t>(o * kBusSrcCount + r)].store(std::isfinite(v) ? juce::jlimit(-2.0f, 2.0f, static_cast<float>(v)) : 0.0f,
                                                             std::memory_order_relaxed);
        }
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
    t.setProperty("bus", packBusRows(bus, 0, kBusSrcLegacyCount), nullptr);
    t.setProperty("busFx2", packBusRows(bus, kBusSrcLegacyCount, kBusSrcCount), nullptr);
    t.setProperty("savedDirect", hasSavedDirect(), nullptr);
    return t;
}

bool RoutingState::fromValueTree(const juce::ValueTree& t) {
    if (!t.isValid() || t.getType().toString() != "Routing" || static_cast<int>(t["version"]) != 1) return false;
    // Validate sizes first so a malformed tree can never leave a half-applied matrix.
    const auto count = [](const juce::String& s) { return juce::StringArray::fromTokens(s, " ", "").size(); };
    if (count(t["direct"].toString()) != kIn * kOut || count(t["assign"].toString()) != kIn * kGroups
        || count(t["bus"].toString()) != kBusSrcLegacyCount * kOut)
        return false;
    // "busFx2" is absent in states saved before FX slots 6/7 existed: those rows are silent. A present but malformed block is rejected.
    const bool hasExtra = t.hasProperty("busFx2");
    if (hasExtra && count(t["busFx2"].toString()) != (kBusSrcCount - kBusSrcLegacyCount) * kOut)
        return false;
    unpack(direct, t["direct"].toString(), -2.0f, 2.0f);
    unpack(assign, t["assign"].toString(), 0.0f, 2.0f);
    unpackBusRows(bus, t["bus"].toString(), 0, kBusSrcLegacyCount);
    unpackBusRows(bus, hasExtra ? t["busFx2"].toString() : juce::String(), kBusSrcLegacyCount, kBusSrcCount);
    setHasSavedDirect(static_cast<bool>(t["savedDirect"]));
    touch();
    return true;
}

juce::String RoutingState::busRowName(int row) {
    if (row >= kBusSrcGroupBase && row < kBusSrcFxBase)
        return "Group " + juce::String((row - kBusSrcGroupBase) / 2 + 1) + ((row & 1) ? " R" : " L");
    if (busRowIsFxReturn(row)) {
        static const char* names[kNumFx] = { "Delay", "Echo", "Tape", "Reverse", "Noise", "Reverb", "Multiplier" };
        return juce::String(names[busRowFxIndex(row)]) + ((row & 1) ? " R" : " L");
    }
    if (row >= kBusSrcPlayback && row < kBusSrcMaster) return (row & 1) ? "Playback R" : "Playback L";
    if (row >= kBusSrcMaster && row < kBusSrcCount) return (row & 1) ? "Master R" : "Master L";
    return {};
}
}
