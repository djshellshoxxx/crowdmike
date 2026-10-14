#include "InputRisk.h"
#include <cmath>

namespace crowdmike {
using namespace params;

namespace {
float gainDb(float linear) noexcept { const float a = std::abs(linear); return a > 1.0e-6f ? 20.0f * std::log10(a) : -120.0f; }
bool on(const ParamBank& b, int idx) noexcept { return b.rawAt(idx) >= 0.5f; }
}

InputRiskReport evaluateInputRisk(const InputRiskContext& c, int i)
{
    InputRiskReport rep;
    const auto& b = c.bank;
    if (i < 0 || i >= kMaxInputs) return rep;
    if (on(b, ParamBank::index(Mod::Input, i, InputP::Mute))) { rep.result = dsp::scoreRisk(rep.inputs); return rep; }

    const float inTrim = b.rawAt(ParamBank::index(Mod::Input, i, InputP::Trim));
    const bool masterOpen = !on(b, ParamBank::index(Mod::Master, 0, MasterP::Mute));
    const float masterDb = b.rawAt(ParamBank::index(Mod::Master, 0, MasterP::Level));

    float bestLoop = -1.0e9f;
    bool bestViaMaster = false;
    const int outs = juce::jlimit(0, kMaxOutputs, c.numOutputs);
    for (int o = 0; o < outs; ++o) {
        if (on(b, ParamBank::index(Mod::Output, o, OutputP::Mute))) continue;
        const float outTrim = b.rawAt(ParamBank::index(Mod::Output, o, OutputP::Trim));
        const auto consider = [&](float groupDb, float busDb, bool viaMaster) {
            dsp::RiskInputs in;
            in.routeExists = true;
            in.inputTrimDb = inTrim;
            in.groupGainDb = groupDb;
            in.busGainDb = busDb;
            in.outputGainDb = outTrim;
            const float loop = dsp::routeLoopGainDb(in);
            if (loop > bestLoop) { bestLoop = loop; rep.inputs = in; rep.worstOutput = o; bestViaMaster = viaMaster; }
        };
        const float direct = c.routing.getDirect(i, o);
        if (direct != 0.0f) consider(0.0f, gainDb(direct), false);
        for (int g = 0; g < kMaxGroups; ++g) {
            const float assign = c.routing.getAssign(i, g);
            if (assign <= 0.0f || on(b, ParamBank::index(Mod::Group, g, GroupP::Mute))) continue;
            const float grpDb = gainDb(assign) + b.rawAt(ParamBank::index(Mod::Group, g, GroupP::Trim));
            const float bus = juce::jmax(std::abs(c.routing.getBus(RoutingState::busRowForGroup(g, 0), o)),
                                         std::abs(c.routing.getBus(RoutingState::busRowForGroup(g, 1), o)));
            if (bus > 0.0f) consider(grpDb, gainDb(bus), false);
            const float mbus = juce::jmax(std::abs(c.routing.getBus(RoutingState::busRowForMaster(0), o)),
                                          std::abs(c.routing.getBus(RoutingState::busRowForMaster(1), o)));
            if (masterOpen && mbus > 0.0f) consider(grpDb, masterDb + gainDb(mbus), true);
        }
    }
    if (!rep.inputs.routeExists) { rep.result = dsp::scoreRisk(rep.inputs); return rep; }

    auto& in = rep.inputs;
    if (on(b, ParamBank::index(Mod::Input, i, InputP::InCompOn)))
        in.compressorMakeupDb = b.rawAt(ParamBank::index(Mod::Input, i, InputP::InCompMakeup));
    // Recirculating effects the input feeds (delay = send 1, tape = send 3).
    if (b.rawAt(ParamBank::index(Mod::Input, i, InputP::Fx1)) > -79.9f && !on(b, ParamBank::index(Mod::FxDelay, 0, DelayP::Bypass)))
        in.delayFeedback = juce::jmax(in.delayFeedback, b.rawAt(ParamBank::index(Mod::FxDelay, 0, DelayP::Feedback)));
    if (b.rawAt(ParamBank::index(Mod::Input, i, InputP::Fx3)) > -79.9f && !on(b, ParamBank::index(Mod::FxTape, 0, TapeP::Bypass)))
        in.delayFeedback = juce::jmax(in.delayFeedback, b.rawAt(ParamBank::index(Mod::FxTape, 0, TapeP::Feedback)));
    if (c.feedbackTap == i + 1 || (c.feedbackTap == 0 && bestViaMaster))
        in.persistentPeakSeverity = juce::jlimit(0.0f, 1.0f, c.tapSeverity);
    const auto& zi = c.show.input(i).zone;
    in.sameZone = zi.isNotEmpty() && rep.worstOutput >= 0 && zi == c.show.output(rep.worstOutput).zone;
    rep.result = dsp::scoreRisk(in);
    return rep;
}
}
