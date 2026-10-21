#include "LineCheckApply.h"
#include "Core/ParamBank.h"

namespace crowdmike::linecheck {
namespace {
juce::String idFor(int input, int paramEnum) { return ParamBank::paramId(Mod::Input, input, paramEnum); }
}

int applyProposals(ApplyTarget& t, const std::vector<Result>& results, const std::vector<int>& inputs, const ApplyOptions& o) {
    namespace P = params::InputP;
    if (o.newTransaction) t.undo.beginNewTransaction("Line check: apply");
    int writes = 0;
    for (const int i : inputs) {
        if (i < 0 || i >= kMaxInputs || i >= static_cast<int>(results.size())) continue;
        const auto& r = results[static_cast<size_t>(i)];
        if (!r.valid || r.verdict == Verdict::Dead) continue;
        if (o.trim && r.hasTrim && !r.trimNoChange) {
            float value = r.proposedTrimDb;
            // Re-check against the live trim: a clipping input is never raised.
            if (r.clips > 0 && t.getParam) value = std::min(value, t.getParam(idFor(i, P::Trim)));
            t.setParam(idFor(i, P::Trim), value);
            ++writes;
        }
        if (o.hpf && r.suggestHpOn && !r.hpNoChange) {
            t.setParam(idFor(i, P::HpOn), 1.0f);
            t.setParam(idFor(i, P::HpHz), r.suggestHpHz);
            writes += 2;
        }
        if (o.polarity && r.polarityMismatch && t.getParam) {
            t.setParam(idFor(i, P::Pol), t.getParam(idFor(i, P::Pol)) >= 0.5f ? 0.0f : 1.0f);
            ++writes;
        }
    }
    return writes;
}

int applyQuickStartDefaults(ApplyTarget& t, int activeInputs) {
    namespace P = params::InputP;
    int writes = 0;
    for (int i = 0; i < std::min(activeInputs, kMaxInputs); ++i) {
        if (t.getParam && t.getParam(idFor(i, P::HpOn)) >= 0.5f) continue;
        t.setParam(idFor(i, P::HpOn), 1.0f);
        t.setParam(idFor(i, P::HpHz), kHpfDefaultHz);
        writes += 2;
    }
    return writes;
}
}
