#pragma once
// Parameter definitions for every engine module (the "contract" between UI, host automation, state and DSP).
//
// * Each module block is an X-macro list; the enum and the Def table are generated from the same list so they
//   can never drift apart.
// * DSP modules receive a `const float*` pointing at one module instance's parameters, indexed by the enum
//   (natural units: dB, Hz, ms, 0/1 for bool, choice index for choice).
// * Parameter IDs are "<instance prefix><suffix>". They are PERMANENT once released. Never rename a suffix; add new ones
//   at the END of a list instead (enum order is not persisted, IDs are).
#include <cstddef>

namespace crowdmike::params {
enum class Kind : unsigned char { Float, Bool, Choice };

struct Def {
    const char* suffix;
    const char* name;
    Kind kind;
    float lo, hi, def, step, mid;   // mid != 0 -> skew so that `mid` sits at the centre of the control
    const char* unit;
    const char* choices;            // '|' separated for Kind::Choice
};

constexpr Def F(const char* s, const char* n, float lo, float hi, float d, const char* unit = "",
                float step = 0.01f, float mid = 0.0f) { return { s, n, Kind::Float, lo, hi, d, step, mid, unit, nullptr }; }
constexpr Def B(const char* s, const char* n, bool d = false) { return { s, n, Kind::Bool, 0.f, 1.f, d ? 1.f : 0.f, 1.f, 0.f, "", nullptr }; }
constexpr Def C(const char* s, const char* n, const char* ch, int d = 0) { return { s, n, Kind::Choice, 0.f, 0.f, static_cast<float>(d), 1.f, 0.f, "", ch }; }

#define CM_SLOPES "12 dB/oct|24 dB/oct|36 dB/oct|48 dB/oct"
#define CM_EQTYPES "Bell|Notch|Low shelf|High shelf"
#define CM_DIVISIONS "1/32|1/16T|1/16|1/8T|1/16.|1/8|1/4T|1/8.|1/4|1/2T|1/4.|1/2|1/1"

// Parametric band: On, Type, Hz, Gain, Q (5 consecutive entries; index = Eq1On + 5*(band-1) + field).
#define CM_EQBAND(X, n, hz, type) \
    X(Eq##n##On, B("eq" #n "On", "EQ " #n " On")) \
    X(Eq##n##Type, C("eq" #n "Type", "EQ " #n " Type", CM_EQTYPES, type)) \
    X(Eq##n##Hz, F("eq" #n "Hz", "EQ " #n " Frequency", 20.f, 20000.f, hz, "Hz", 1.f, 1000.f)) \
    X(Eq##n##Gain, F("eq" #n "Gain", "EQ " #n " Gain", -24.f, 24.f, 0.f, "dB", 0.1f)) \
    X(Eq##n##Q, F("eq" #n "Q", "EQ " #n " Q", 0.1f, 30.f, 1.f, "", 0.01f, 1.f))
constexpr int kEqBandStride = 5;
enum EqField { EqOn = 0, EqType = 1, EqHz = 2, EqGain = 3, EqQ = 4 };
enum EqTypeId { EqBell = 0, EqNotch = 1, EqLowShelf = 2, EqHighShelf = 3 };

// Compressor block (10 entries; same layout wherever a compressor appears). Index = <Prefix>Comp + field.
#define CM_COMP(X, P) \
    X(P##CompOn, B("compOn", "Comp On")) \
    X(P##CompThr, F("compThr", "Comp Threshold", -60.f, 0.f, -18.f, "dB", 0.1f)) \
    X(P##CompRatio, F("compRatio", "Comp Ratio", 1.f, 20.f, 2.5f, ":1", 0.1f, 4.f)) \
    X(P##CompAttack, F("compAttack", "Comp Attack", 0.1f, 200.f, 10.f, "ms", 0.1f, 20.f)) \
    X(P##CompRelease, F("compRelease", "Comp Release", 5.f, 2000.f, 120.f, "ms", 1.f, 200.f)) \
    X(P##CompKnee, F("compKnee", "Comp Knee", 0.f, 24.f, 6.f, "dB", 0.1f)) \
    X(P##CompMakeup, F("compMakeup", "Comp Makeup", -12.f, 24.f, 0.f, "dB", 0.1f)) \
    X(P##CompAuto, B("compAutoMakeup", "Comp Auto Makeup")) \
    X(P##CompScHpf, F("compScHpf", "Comp Sidechain HPF", 20.f, 1000.f, 80.f, "Hz", 1.f, 150.f)) \
    X(P##CompMix, F("compMix", "Comp Mix", 0.f, 1.f, 1.f, "", 0.01f))
namespace CompP { enum { On, Thr, Ratio, Attack, Release, Knee, Makeup, AutoMakeup, ScHpf, Mix, Count }; }

namespace GateP { enum { On, Thr, Range, Ratio, Attack, Hold, Release, Hyst, ScHpf, ScLpf, Count }; }
namespace DeEssP { enum { On, Freq, Thr, Ratio, Range, Attack, Release, Listen, Mode, Count }; }
namespace TransP { enum { On, AttackGain, SustainGain, Sens, Mix, Count }; }
namespace LimP { enum { On, Ceiling, Release, Drive, Count }; }
namespace WidthP { enum { On, Width, Decorr, HaasMs, MonoCompat, LfMonoHz, Count }; }

#define CM_GATE(X) \
    X(GateOn, B("gateOn", "Gate On")) \
    X(GateThr, F("gateThr", "Gate Threshold", -80.f, 0.f, -60.f, "dB", 0.1f)) \
    X(GateRange, F("gateRange", "Gate Range", -80.f, 0.f, -24.f, "dB", 0.1f)) \
    X(GateRatio, F("gateRatio", "Gate Ratio", 1.f, 20.f, 2.f, ":1", 0.1f, 4.f)) \
    X(GateAttack, F("gateAttack", "Gate Attack", 0.1f, 100.f, 5.f, "ms", 0.1f, 10.f)) \
    X(GateHold, F("gateHold", "Gate Hold", 0.f, 1000.f, 50.f, "ms", 1.f, 100.f)) \
    X(GateRelease, F("gateRelease", "Gate Release", 5.f, 2000.f, 200.f, "ms", 1.f, 300.f)) \
    X(GateHyst, F("gateHyst", "Gate Hysteresis", 0.f, 12.f, 3.f, "dB", 0.1f)) \
    X(GateScHpf, F("gateScHpf", "Gate Sidechain HPF", 20.f, 2000.f, 100.f, "Hz", 1.f, 300.f)) \
    X(GateScLpf, F("gateScLpf", "Gate Sidechain LPF", 1000.f, 20000.f, 12000.f, "Hz", 1.f, 6000.f))

#define CM_DEESS(X) \
    X(DeEssOn, B("deessOn", "De-ess On")) \
    X(DeEssFreq, F("deessFreq", "De-ess Frequency", 2000.f, 12000.f, 6500.f, "Hz", 1.f, 5000.f)) \
    X(DeEssThr, F("deessThr", "De-ess Threshold", -60.f, 0.f, -24.f, "dB", 0.1f)) \
    X(DeEssRatio, F("deessRatio", "De-ess Ratio", 1.f, 10.f, 3.f, ":1", 0.1f)) \
    X(DeEssRange, F("deessRange", "De-ess Range", 0.f, 24.f, 12.f, "dB", 0.1f)) \
    X(DeEssAttack, F("deessAttack", "De-ess Attack", 0.05f, 20.f, 1.f, "ms", 0.05f, 2.f)) \
    X(DeEssRelease, F("deessRelease", "De-ess Release", 5.f, 500.f, 60.f, "ms", 1.f, 80.f)) \
    X(DeEssListen, B("deessListen", "De-ess Listen")) \
    X(DeEssMode, C("deessMode", "De-ess Mode", "Split-band|Wideband", 0))

#define CM_TRANS(X) \
    X(TransOn, B("transOn", "Transient On")) \
    X(TransAttack, F("transAttack", "Transient Attack", -12.f, 12.f, 0.f, "dB", 0.1f)) \
    X(TransSustain, F("transSustain", "Transient Sustain", -12.f, 12.f, 0.f, "dB", 0.1f)) \
    X(TransSens, F("transSens", "Transient Sensitivity", 0.f, 1.f, 0.5f)) \
    X(TransMix, F("transMix", "Transient Mix", 0.f, 1.f, 1.f))

#define CM_LIMITER(X, P, ceil, on) \
    X(P##LimOn, B("limOn", "Limiter On", on)) \
    X(P##LimCeil, F("limCeil", "Limiter Ceiling", -12.f, 0.f, ceil, "dB", 0.1f)) \
    X(P##LimRelease, F("limRelease", "Limiter Release", 5.f, 1000.f, 100.f, "ms", 1.f, 100.f)) \
    X(P##LimDrive, F("limDrive", "Limiter Input Gain", 0.f, 24.f, 0.f, "dB", 0.1f))

// ---------------------------------------------------------------- Input strips (16 instances, "input<n>.")
// Legacy IDs (trimDb, mute, polarity, highPass*, lowPass*) are frozen from the first release.
#define CM_INPUT_PARAMS(X) \
    X(Trim, F("trimDb", "Trim", -60.f, 24.f, 0.f, "dB", 0.01f)) \
    X(Mute, B("mute", "Mute")) \
    X(Solo, B("solo", "Solo")) \
    X(Pol, B("polarity", "Polarity Invert")) \
    X(Pan, F("pan", "Pan", -1.f, 1.f, 0.f)) \
    X(Link, B("linkNext", "Stereo Link With Next")) \
    X(HpOn, B("highPassEnabled", "High Pass")) \
    X(HpHz, F("highPassHz", "High Pass Frequency", 20.f, 500.f, 80.f, "Hz", 1.f, 100.f)) \
    X(HpSlope, C("highPassSlope", "High Pass Slope", CM_SLOPES, 0)) \
    X(LpOn, B("lowPassEnabled", "Low Pass")) \
    X(LpHz, F("lowPassHz", "Low Pass Frequency", 2000.f, 20000.f, 18000.f, "Hz", 1.f, 8000.f)) \
    X(LpSlope, C("lowPassSlope", "Low Pass Slope", CM_SLOPES, 0)) \
    X(NotchOn, B("notchOn", "Notch")) \
    X(NotchHz, F("notchHz", "Notch Frequency", 100.f, 10000.f, 1000.f, "Hz", 1.f, 1000.f)) \
    X(NotchQ, F("notchQ", "Notch Q", 2.f, 60.f, 20.f, "", 0.1f, 15.f)) \
    X(NotchDepth, F("notchDepthDb", "Notch Depth", -24.f, 0.f, -12.f, "dB", 0.1f)) \
    CM_EQBAND(X, 1, 120.f, 2) \
    CM_EQBAND(X, 2, 400.f, 0) \
    CM_EQBAND(X, 3, 2500.f, 0) \
    CM_EQBAND(X, 4, 8000.f, 3) \
    X(PhaseStages, C("phaseStages", "Phase Stages", "Off|1|2|3|4", 0)) \
    X(PhaseHz, F("phaseHz", "Phase Frequency", 20.f, 5000.f, 500.f, "Hz", 1.f, 500.f)) \
    X(AlignMs, F("alignDelayMs", "Alignment Delay", 0.f, 500.f, 0.f, "ms", 0.01f, 50.f)) \
    CM_GATE(X) \
    CM_DEESS(X) \
    CM_COMP(X, In) \
    CM_TRANS(X) \
    X(Riding, B("gainRiding", "Gain Riding")) \
    X(Fx1, F("fxSend1", "Delay Send", -80.f, 6.f, -80.f, "dB", 0.1f)) \
    X(Fx2, F("fxSend2", "Echo Send", -80.f, 6.f, -80.f, "dB", 0.1f)) \
    X(Fx3, F("fxSend3", "Tape Send", -80.f, 6.f, -80.f, "dB", 0.1f)) \
    X(Fx4, F("fxSend4", "Reverse Send", -80.f, 6.f, -80.f, "dB", 0.1f)) \
    X(Fx5, F("fxSend5", "Noise Send", -80.f, 6.f, -80.f, "dB", 0.1f)) \
    X(FxPre, B("fxSendPre", "FX Sends Pre-fader"))

// ---------------------------------------------------------------- Groups (8 instances, "group<n>.")
#define CM_GROUP_PARAMS(X) \
    X(Trim, F("trimDb", "Trim", -60.f, 24.f, 0.f, "dB", 0.01f)) \
    X(Mute, B("mute", "Mute")) \
    X(Solo, B("solo", "Solo")) \
    X(Pol, B("polarity", "Polarity Invert")) \
    X(Pan, F("pan", "Balance", -1.f, 1.f, 0.f)) \
    X(DelayMs, F("delayMs", "Reinforcement Delay", 0.f, 500.f, 0.f, "ms", 0.01f, 50.f)) \
    X(HpOn, B("hpOn", "High Pass")) \
    X(HpHz, F("hpHz", "High Pass Frequency", 20.f, 500.f, 80.f, "Hz", 1.f, 100.f)) \
    X(LpOn, B("lpOn", "Low Pass")) \
    X(LpHz, F("lpHz", "Low Pass Frequency", 2000.f, 20000.f, 18000.f, "Hz", 1.f, 8000.f)) \
    CM_EQBAND(X, 1, 120.f, 2) \
    CM_EQBAND(X, 2, 400.f, 0) \
    CM_EQBAND(X, 3, 2500.f, 0) \
    CM_EQBAND(X, 4, 8000.f, 3) \
    CM_COMP(X, Grp) \
    CM_LIMITER(X, Grp, -1.f, false) \
    X(Width, F("width", "Width", 0.f, 2.f, 1.f)) \
    X(Fx1, F("fxSend1", "Delay Send", -80.f, 6.f, -80.f, "dB", 0.1f)) \
    X(Fx2, F("fxSend2", "Echo Send", -80.f, 6.f, -80.f, "dB", 0.1f)) \
    X(Fx3, F("fxSend3", "Tape Send", -80.f, 6.f, -80.f, "dB", 0.1f)) \
    X(Fx4, F("fxSend4", "Reverse Send", -80.f, 6.f, -80.f, "dB", 0.1f)) \
    X(Fx5, F("fxSend5", "Noise Send", -80.f, 6.f, -80.f, "dB", 0.1f))

// ---------------------------------------------------------------- Output channels (16 instances, "out<n>.")
#define CM_OUTPUT_PARAMS(X) \
    X(Trim, F("trimDb", "Trim", -60.f, 12.f, 0.f, "dB", 0.01f)) \
    X(Mute, B("mute", "Mute")) \
    X(Pol, B("polarity", "Polarity Invert")) \
    X(DelayMs, F("delayMs", "Output Delay", 0.f, 500.f, 0.f, "ms", 0.01f, 50.f)) \
    X(HpOn, B("hpOn", "High Pass")) \
    X(HpHz, F("hpHz", "High Pass Frequency", 20.f, 500.f, 30.f, "Hz", 1.f, 100.f)) \
    X(LpOn, B("lpOn", "Low Pass")) \
    X(LpHz, F("lpHz", "Low Pass Frequency", 2000.f, 20000.f, 20000.f, "Hz", 1.f, 8000.f)) \
    X(LimOn, B("limiterOn", "Limiter", true))

// ---------------------------------------------------------------- Master crowd bus ("master.")
#define CM_MASTER_PARAMS(X) \
    X(Level, F("levelDb", "Master Level", -80.f, 12.f, 0.f, "dB", 0.1f)) \
    X(Mute, B("mute", "Master Mute")) \
    X(EqOn, B("eqOn", "Master EQ On", true)) \
    CM_EQBAND(X, 1, 60.f, 2) \
    CM_EQBAND(X, 2, 150.f, 0) \
    CM_EQBAND(X, 3, 400.f, 0) \
    CM_EQBAND(X, 4, 1000.f, 0) \
    CM_EQBAND(X, 5, 2500.f, 0) \
    CM_EQBAND(X, 6, 5000.f, 0) \
    CM_EQBAND(X, 7, 10000.f, 0) \
    CM_EQBAND(X, 8, 16000.f, 3) \
    CM_COMP(X, Mst) \
    CM_LIMITER(X, Mst, -1.f, true) \
    X(WidthOn, B("widthOn", "Width On")) \
    X(Width, F("width", "Width", 0.f, 2.f, 1.f)) \
    X(Decorr, F("decorr", "Decorrelation", 0.f, 1.f, 0.f)) \
    X(Haas, F("haasMs", "Haas Offset", 0.f, 20.f, 0.f, "ms", 0.1f)) \
    X(MonoCompat, F("monoCompat", "Mono Compatibility", 0.f, 1.f, 0.f)) \
    X(LfMono, F("lfMonoHz", "Low-frequency Mono Cutoff", 20.f, 500.f, 120.f, "Hz", 1.f, 150.f))

// ---------------------------------------------------------------- Global ("" prefix)
// "limiterCeilingDb" is the frozen first-release ID: the output-stage limiter ceiling.
#define CM_GLOBAL_PARAMS(X) \
    X(OutCeiling, F("limiterCeilingDb", "Limiter Ceiling", -12.f, 0.f, -0.5f, "dB", 0.1f)) \
    X(Bypass, B("bypass", "Bypass")) \
    X(Panic, B("panic", "Panic Mute")) \
    X(Rehearsal, B("rehearsal", "Rehearsal Mode (no output)")) \
    X(UnlockLimiters, B("unlockLimiters", "Unlock Output Limiters")) \
    X(OutLimRelease, F("outLimiterRelease", "Output Limiter Release", 5.f, 1000.f, 100.f, "ms", 1.f, 100.f)) \
    X(PlaybackLevel, F("playbackLevelDb", "Playback Level", -80.f, 12.f, 0.f, "dB", 0.1f)) \
    X(Freeze, B("freeze", "Freeze / Hold")) \
    X(DensityToSend, F("densityToSend", "Crowd Density To FX Sends", 0.f, 1.f, 0.f)) \
    X(Soundcheck, B("soundcheck", "Virtual Soundcheck")) \
    X(RecordAll, B("recordAll", "Record All")) \
    X(SceneNext, B("sceneNext", "Next Scene")) \
    X(ScenePrev, B("scenePrev", "Previous Scene"))

// ---------------------------------------------------------------- Effects ("fx.<name>.")
#define CM_FX_DELAY(X) \
    X(Bypass, B("bypass", "Delay Bypass")) \
    X(Mode, C("mode", "Delay Mode", "Single|Ping-pong|Multi-tap", 0)) \
    X(Sync, B("sync", "Delay Tempo Sync")) \
    X(TimeMs, F("timeMs", "Delay Time", 1.f, 2000.f, 350.f, "ms", 0.1f, 400.f)) \
    X(Division, C("division", "Delay Division", CM_DIVISIONS, 8)) \
    X(Feedback, F("feedback", "Delay Feedback", 0.f, 0.95f, 0.35f)) \
    X(Mix, F("mix", "Delay Wet/Dry", 0.f, 1.f, 1.f)) \
    X(Hpf, F("hpf", "Delay Feedback HPF", 20.f, 2000.f, 200.f, "Hz", 1.f, 300.f)) \
    X(Lpf, F("lpf", "Delay Feedback LPF", 1000.f, 20000.f, 6000.f, "Hz", 1.f, 6000.f)) \
    X(Offset, F("offsetMs", "Delay Stereo Offset", 0.f, 50.f, 0.f, "ms", 0.1f)) \
    X(Diffusion, F("diffusion", "Delay Diffusion", 0.f, 1.f, 0.f)) \
    X(Taps, F("taps", "Delay Taps", 2.f, 6.f, 4.f, "", 1.f)) \
    X(InGain, F("inDb", "Delay Input", -24.f, 12.f, 0.f, "dB", 0.1f)) \
    X(OutGain, F("outDb", "Delay Output", -24.f, 12.f, 0.f, "dB", 0.1f)) \
    X(Return, F("returnDb", "Delay Return", -80.f, 6.f, 0.f, "dB", 0.1f))

#define CM_FX_ECHO(X) \
    X(Bypass, B("bypass", "Echo Bypass")) \
    X(TimeMs, F("timeMs", "Echo Time", 20.f, 2000.f, 450.f, "ms", 1.f, 450.f)) \
    X(CaptureMs, F("captureMs", "Echo Capture Length", 50.f, 4000.f, 1000.f, "ms", 1.f, 1000.f)) \
    X(Repeats, F("repeats", "Echo Repeats", 1.f, 16.f, 4.f, "", 1.f)) \
    X(Decay, F("decay", "Echo Decay", 0.f, 0.95f, 0.5f)) \
    X(Send, F("send", "Echo Send Amount", 0.f, 1.f, 1.f)) \
    X(Hpf, F("hpf", "Echo HPF", 20.f, 2000.f, 150.f, "Hz", 1.f, 300.f)) \
    X(Lpf, F("lpf", "Echo LPF", 1000.f, 20000.f, 7000.f, "Hz", 1.f, 6000.f)) \
    X(Trigger, B("trigger", "Echo Throw")) \
    X(TailKill, B("tailKill", "Echo Tail Kill")) \
    X(Return, F("returnDb", "Echo Return", -80.f, 6.f, 0.f, "dB", 0.1f))

#define CM_FX_TAPE(X) \
    X(Bypass, B("bypass", "Tape Bypass")) \
    X(TimeMs, F("timeMs", "Tape Time", 20.f, 2000.f, 380.f, "ms", 1.f, 400.f)) \
    X(Feedback, F("feedback", "Tape Feedback", 0.f, 0.92f, 0.4f)) \
    X(Drive, F("drive", "Tape Drive", 0.f, 1.f, 0.3f)) \
    X(Age, F("age", "Tape Age", 0.f, 1.f, 0.3f)) \
    X(Wow, F("wow", "Tape Wow", 0.f, 1.f, 0.2f)) \
    X(Flutter, F("flutter", "Tape Flutter", 0.f, 1.f, 0.2f)) \
    X(LowCut, F("lowCut", "Tape Low Cut", 20.f, 1000.f, 150.f, "Hz", 1.f, 200.f)) \
    X(HighCut, F("highCut", "Tape High Cut", 1000.f, 20000.f, 5000.f, "Hz", 1.f, 5000.f)) \
    X(Noise, F("noise", "Tape Noise", 0.f, 1.f, 0.1f)) \
    X(Mix, F("mix", "Tape Wet/Dry", 0.f, 1.f, 1.f)) \
    X(Heads, F("heads", "Tape Head Offset", 0.f, 1.f, 0.5f)) \
    X(Return, F("returnDb", "Tape Return", -80.f, 6.f, 0.f, "dB", 0.1f))

#define CM_FX_REVERSE(X) \
    X(Bypass, B("bypass", "Reverse Bypass")) \
    X(Mode, C("mode", "Reverse Mode", "Triggered|Continuous|Repeat", 0)) \
    X(ChunkSec, F("chunkSec", "Reverse Chunk Length", 0.25f, 8.f, 4.f, "s", 0.01f, 3.f)) \
    X(TrigMode, C("trigMode", "Reverse Trigger Mode", "Manual|Repeating|Transient|Beat-sync", 0)) \
    X(Mix, F("mix", "Reverse Wet/Dry", 0.f, 1.f, 1.f)) \
    X(Crossfade, F("crossfadeMs", "Reverse Crossfade", 5.f, 500.f, 50.f, "ms", 1.f, 80.f)) \
    X(Retrigger, C("retrigger", "Reverse Retrigger", "Restart|Ignore|Queue", 0)) \
    X(StereoLink, B("stereoLink", "Reverse Stereo Link", true)) \
    X(Source, C("source", "Reverse Source", "Post-group sends|Pre-group inputs", 0)) \
    X(Decay, F("decay", "Reverse Repeat Decay", 0.f, 1.f, 0.6f)) \
    X(BeatDiv, C("beatDiv", "Reverse Beat Division", "1 beat|2 beats|1 bar|2 bars|4 bars", 2)) \
    X(Sens, F("sens", "Reverse Transient Sensitivity", 0.f, 1.f, 0.6f)) \
    X(Trigger, B("trigger", "Reverse Trigger")) \
    X(Return, F("returnDb", "Reverse Return", -80.f, 6.f, 0.f, "dB", 0.1f))

#define CM_FX_NOISE(X) \
    X(Bypass, B("bypass", "Noise Bypass")) \
    X(Amount, F("amount", "Noise Amount", 0.f, 1.f, 0.1f)) \
    X(Color, C("color", "Noise Color", "White|Pink|Brown|Bright", 1)) \
    X(Tilt, F("tilt", "Noise Tilt", -1.f, 1.f, 0.f)) \
    X(Hpf, F("hpf", "Noise HPF", 20.f, 2000.f, 300.f, "Hz", 1.f, 400.f)) \
    X(Lpf, F("lpf", "Noise LPF", 1000.f, 20000.f, 12000.f, "Hz", 1.f, 6000.f)) \
    X(Follow, F("follow", "Noise Crowd Follow", 0.f, 1.f, 0.7f)) \
    X(TransFollow, F("transFollow", "Noise Transient Follow", 0.f, 1.f, 0.f)) \
    X(Attack, F("attack", "Noise Attack", 1.f, 500.f, 20.f, "ms", 1.f, 50.f)) \
    X(Release, F("release", "Noise Release", 10.f, 2000.f, 300.f, "ms", 1.f, 300.f)) \
    X(Width, F("width", "Noise Width", 0.f, 1.f, 0.8f)) \
    X(Seed, F("seed", "Noise Seed", 1.f, 9999.f, 1.f, "", 1.f)) \
    X(Return, F("returnDb", "Noise Return", -80.f, 6.f, -12.f, "dB", 0.1f))

#define CM_MACRO_PARAMS(X) X(Value, F("", "Macro", 0.f, 1.f, 0.f))

// ---------------------------------------------------------------- Tempo / Beat Sync ("tempo.") -- docs/BEAT_SYNC_SPEC.md
// Appended after the macros so every earlier global index is unchanged. The Tap and Resync entries are momentary
// buttons (never stored in scenes). Beats-per-bar choice index -> {4, 3, 2, 5, 6, 7}.
#define CM_AUTO_SOURCES "Master|Input 1|Input 2|Input 3|Input 4|Input 5|Input 6|Input 7|Input 8|Input 9|Input 10|Input 11|Input 12|Input 13|Input 14|Input 15|Input 16"
#define CM_TEMPO_PARAMS(X) \
    X(Source, C("source", "Tempo Source", "Host|Tap|Manual|MIDI Clock|Auto", 0)) \
    X(ManualBpm, F("manualBpm", "Manual BPM", 20.f, 300.f, 120.f, "BPM", 0.01f, 120.f)) \
    X(Nudge, F("nudgeBpm", "Tempo Nudge", -10.f, 10.f, 0.f, "BPM", 0.01f)) \
    X(Quantise, C("quantise", "Trigger Quantise", "Off|1/4|1/2|1 bar", 0)) \
    X(AutoSource, C("autoSource", "Auto Tempo Input", CM_AUTO_SOURCES, 0)) \
    X(BarBeats, C("barBeats", "Beats Per Bar", "4|3|2|5|6|7", 0)) \
    X(Tap, B("tap", "Tap Tempo")) \
    X(Resync, B("resync", "Resync Downbeat"))
constexpr int kBarBeatsTable[6] = { 4, 3, 2, 5, 6, 7 };

// ---------------------------------------------------------------- generate enums + tables
#define CM_ENUM_ENTRY(e, d) e,
#define CM_DEF_ENTRY(e, d) d,
#define CM_DEFINE_MODULE(Name, LIST) \
    namespace Name { enum : int { LIST(CM_ENUM_ENTRY) Count }; \
    inline constexpr Def defs[] = { LIST(CM_DEF_ENTRY) }; \
    static_assert(sizeof(defs) / sizeof(defs[0]) == static_cast<size_t>(Count), #Name " table/enum mismatch"); }

CM_DEFINE_MODULE(InputP, CM_INPUT_PARAMS)
CM_DEFINE_MODULE(GroupP, CM_GROUP_PARAMS)
CM_DEFINE_MODULE(OutputP, CM_OUTPUT_PARAMS)
CM_DEFINE_MODULE(MasterP, CM_MASTER_PARAMS)
CM_DEFINE_MODULE(GlobalP, CM_GLOBAL_PARAMS)
CM_DEFINE_MODULE(DelayP, CM_FX_DELAY)
CM_DEFINE_MODULE(EchoP, CM_FX_ECHO)
CM_DEFINE_MODULE(TapeP, CM_FX_TAPE)
CM_DEFINE_MODULE(ReverseP, CM_FX_REVERSE)
CM_DEFINE_MODULE(NoiseP, CM_FX_NOISE)
CM_DEFINE_MODULE(TempoP, CM_TEMPO_PARAMS)

// Contiguous sub-blocks inside the larger tables (so DSP can be handed `base + offset`).
// Verified against the tables in tests (ParamTablesTest).
constexpr int inputEqBase() { return InputP::Eq1On; }
constexpr int inputGateBase() { return InputP::GateOn; }
constexpr int inputDeEssBase() { return InputP::DeEssOn; }
constexpr int inputCompBase() { return InputP::InCompOn; }
constexpr int inputTransBase() { return InputP::TransOn; }
constexpr int groupEqBase() { return GroupP::Eq1On; }
constexpr int groupCompBase() { return GroupP::GrpCompOn; }
constexpr int groupLimBase() { return GroupP::GrpLimOn; }
constexpr int masterEqBase() { return MasterP::Eq1On; }
constexpr int masterCompBase() { return MasterP::MstCompOn; }
constexpr int masterLimBase() { return MasterP::MstLimOn; }
constexpr int masterWidthBase() { return MasterP::WidthOn; }
}
