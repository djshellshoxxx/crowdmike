#pragma once
// Compile-time capacity limits (PRODUCT_SPEC section 7). All engine storage is sized from these.
namespace crowdmike {
constexpr int kMaxInputs = 16;
constexpr int kMaxGroups = 8;
constexpr int kMaxOutputs = 16;      // physical/host output channels
constexpr int kNumLegacyFx = 5;      // Delay, Echo, Tape, Reverse, Noise (first-release layout; persisted indices below depend on it)
constexpr int kNumFx = 7;            // + Stadium Reverb (6), Crowd Multiplier (7)
constexpr int kMaxRecordTracks = 32;
constexpr int kMaxClips = 32;
constexpr int kNumMacros = 8;
constexpr int kNumScenes = 32;
constexpr int kMaxSceneNameLen = 48;

// Output-matrix bus sources (processed inputs are handled by the legacy direct RoutingMatrix).
// PERSISTED LAYOUT (ROUTING_SPEC section 3): rows 0..29 are frozen from the 5-FX releases. The FX returns added later
// (slots 6 and 7) are APPENDED after the master rows so no saved row index ever changes meaning.
constexpr int kBusSrcGroupBase = 0;                       // 16 rows: G1L,G1R,G2L,...
constexpr int kBusSrcFxBase = kBusSrcGroupBase + kMaxGroups * 2;   // 10 rows: Fx1L,Fx1R,...Fx5R (legacy FX only)
constexpr int kBusSrcPlayback = kBusSrcFxBase + kNumLegacyFx * 2;  // 2 rows (26, 27)
constexpr int kBusSrcMaster = kBusSrcPlayback + 2;                 // 2 rows (28, 29)
constexpr int kBusSrcLegacyCount = kBusSrcMaster + 2;              // 30
constexpr int kBusSrcFxExtraBase = kBusSrcLegacyCount;             // (kNumFx - kNumLegacyFx) * 2 rows: Fx6L, Fx6R, Fx7L, Fx7R (30..33)
constexpr int kBusSrcCount = kBusSrcFxExtraBase + (kNumFx - kNumLegacyFx) * 2;   // 34

// Bus row of FX return `fx` (0-based), channel 0 = L, 1 = R. Slots >= kNumLegacyFx live in the appended block.
constexpr int busRowForFxReturn(int fx, int channel) noexcept {
    return fx < kNumLegacyFx ? kBusSrcFxBase + fx * 2 + channel : kBusSrcFxExtraBase + (fx - kNumLegacyFx) * 2 + channel;
}
constexpr bool busRowIsFxReturn(int row) noexcept {
    return (row >= kBusSrcFxBase && row < kBusSrcPlayback) || (row >= kBusSrcFxExtraBase && row < kBusSrcCount);
}
// FX slot (0-based) of an FX-return bus row; -1 for any other row.
constexpr int busRowFxIndex(int row) noexcept {
    if (row >= kBusSrcFxBase && row < kBusSrcPlayback) return (row - kBusSrcFxBase) / 2;
    if (row >= kBusSrcFxExtraBase && row < kBusSrcCount) return kNumLegacyFx + (row - kBusSrcFxExtraBase) / 2;
    return -1;
}
}
