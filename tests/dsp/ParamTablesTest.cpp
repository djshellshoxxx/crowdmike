#include "TestHarness.h"
#include "Core/ParamTables.h"
#include <cstring>
#include <set>
#include <string>

using namespace crowdmike::params;

namespace {
template <size_t N> void checkUnique(const Def (&defs)[N], const char* module) {
    std::set<std::string> seen;
    for (const auto& d : defs) {
        CM_EXPECT(seen.insert(d.suffix).second);
        CM_EXPECT(d.name != nullptr && std::strlen(d.name) > 0);
        if (d.kind == Kind::Float) CM_EXPECT(d.lo < d.hi && d.def >= d.lo && d.def <= d.hi);
        if (d.kind == Kind::Choice) CM_EXPECT(d.choices != nullptr);
    }
    (void) module;
}
}

CM_TEST(param_tables_are_unique_and_in_range) {
    checkUnique(InputP::defs, "input");
    checkUnique(GroupP::defs, "group");
    checkUnique(OutputP::defs, "output");
    checkUnique(MasterP::defs, "master");
    checkUnique(GlobalP::defs, "global");
    checkUnique(DelayP::defs, "delay");
    checkUnique(EchoP::defs, "echo");
    checkUnique(TapeP::defs, "tape");
    checkUnique(ReverseP::defs, "reverse");
    checkUnique(NoiseP::defs, "noise");
    checkUnique(ReverbP::defs, "reverb");
    checkUnique(MultiP::defs, "multi");
}

CM_TEST(param_tables_fx_slots_6_and_7_are_appended) {
    // Sends 6/7 sit at the END of the input and group tables; every earlier position and ID is untouched.
    CM_EXPECT(InputP::Fx6 == InputP::Count - 2 && InputP::Fx7 == InputP::Count - 1);
    CM_EXPECT(GroupP::Fx6 == GroupP::Count - 2 && GroupP::Fx7 == GroupP::Count - 1);
    CM_EXPECT(InputP::FxPre == InputP::Fx5 + 1);
    CM_EXPECT(std::strcmp(InputP::defs[InputP::Fx6].suffix, "fxSend6") == 0 && std::strcmp(InputP::defs[InputP::Fx7].suffix, "fxSend7") == 0);
    CM_EXPECT(std::strcmp(GroupP::defs[GroupP::Fx7].suffix, "fxSend7") == 0);
    for (int f = 0; f < 7; ++f) {
        const std::string expect = "fxSend" + std::to_string(f + 1);
        CM_EXPECT(expect == InputP::defs[inputSendParam(f)].suffix);
        CM_EXPECT(expect == GroupP::defs[groupSendParam(f)].suffix);
        CM_EXPECT(InputP::defs[inputSendParam(f)].def == -80.0f && GroupP::defs[groupSendParam(f)].lo == -80.0f);
    }
    CM_EXPECT(std::strcmp(ReverbP::defs[ReverbP::On].suffix, "on") == 0 && ReverbP::defs[ReverbP::On].def == 0.0f);
    CM_EXPECT(std::strcmp(ReverbP::defs[ReverbP::TailKill].suffix, "tailKill") == 0);   // OSC pulse + momentary bindings key on this suffix
    CM_EXPECT(std::strcmp(ReverbP::defs[ReverbP::Return].suffix, "returnDb") == 0 && std::strcmp(MultiP::defs[MultiP::Return].suffix, "returnDb") == 0);
    CM_EXPECT(ReverbP::defs[ReverbP::Decay].lo == 0.2f && ReverbP::defs[ReverbP::Decay].hi == 20.0f);
    CM_EXPECT(ReverbP::defs[ReverbP::PreDelay].hi == 250.0f);
    CM_EXPECT(MultiP::defs[MultiP::Voices].lo == 2.0f && MultiP::defs[MultiP::Voices].hi == 16.0f);
}

CM_TEST(param_tables_keep_frozen_first_release_ids) {
    CM_EXPECT(std::strcmp(InputP::defs[InputP::Trim].suffix, "trimDb") == 0);
    CM_EXPECT(std::strcmp(InputP::defs[InputP::Mute].suffix, "mute") == 0);
    CM_EXPECT(std::strcmp(InputP::defs[InputP::Pol].suffix, "polarity") == 0);
    CM_EXPECT(std::strcmp(InputP::defs[InputP::HpOn].suffix, "highPassEnabled") == 0);
    CM_EXPECT(std::strcmp(InputP::defs[InputP::HpHz].suffix, "highPassHz") == 0);
    CM_EXPECT(std::strcmp(InputP::defs[InputP::LpOn].suffix, "lowPassEnabled") == 0);
    CM_EXPECT(std::strcmp(InputP::defs[InputP::LpHz].suffix, "lowPassHz") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[GlobalP::OutCeiling].suffix, "limiterCeilingDb") == 0);
}

CM_TEST(param_sub_blocks_are_contiguous_and_match_dsp_layouts) {
    CM_EXPECT(std::strcmp(InputP::defs[inputCompBase() + CompP::Mix].suffix, "compMix") == 0);
    CM_EXPECT(std::strcmp(InputP::defs[inputCompBase() + CompP::Thr].suffix, "compThr") == 0);
    CM_EXPECT(std::strcmp(InputP::defs[inputGateBase() + GateP::ScLpf].suffix, "gateScLpf") == 0);
    CM_EXPECT(std::strcmp(InputP::defs[inputDeEssBase() + DeEssP::Mode].suffix, "deessMode") == 0);
    CM_EXPECT(std::strcmp(InputP::defs[inputTransBase() + TransP::Mix].suffix, "transMix") == 0);
    CM_EXPECT(std::strcmp(GroupP::defs[groupCompBase() + CompP::Release].suffix, "compRelease") == 0);
    CM_EXPECT(std::strcmp(GroupP::defs[groupLimBase() + LimP::Drive].suffix, "limDrive") == 0);
    CM_EXPECT(std::strcmp(MasterP::defs[masterCompBase() + CompP::ScHpf].suffix, "compScHpf") == 0);
    CM_EXPECT(std::strcmp(MasterP::defs[masterLimBase() + LimP::Ceiling].suffix, "limCeil") == 0);
    CM_EXPECT(std::strcmp(MasterP::defs[masterWidthBase() + WidthP::LfMonoHz].suffix, "lfMonoHz") == 0);
    CM_EXPECT(std::strcmp(MasterP::defs[masterEqBase() + 7 * kEqBandStride + EqQ].suffix, "eq8Q") == 0);
    CM_EXPECT(std::strcmp(InputP::defs[inputEqBase() + 3 * kEqBandStride + EqType].suffix, "eq4Type") == 0);
}
