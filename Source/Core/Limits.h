#pragma once
// Compile-time capacity limits (PRODUCT_SPEC section 7). All engine storage is sized from these.
namespace crowdmike {
constexpr int kMaxInputs = 16;
constexpr int kMaxGroups = 8;
constexpr int kMaxOutputs = 16;      // physical/host output channels
constexpr int kNumFx = 5;            // Delay, Echo, Tape, Reverse, Noise
constexpr int kMaxRecordTracks = 32;
constexpr int kMaxClips = 32;
constexpr int kNumMacros = 8;
constexpr int kNumScenes = 32;
constexpr int kMaxSceneNameLen = 48;

// Output-matrix bus sources (processed inputs are handled by the legacy direct RoutingMatrix).
constexpr int kBusSrcGroupBase = 0;                       // 16 rows: G1L,G1R,G2L,...
constexpr int kBusSrcFxBase = kBusSrcGroupBase + kMaxGroups * 2;   // 10 rows: Fx1L,Fx1R,...
constexpr int kBusSrcPlayback = kBusSrcFxBase + kNumFx * 2;        // 2 rows
constexpr int kBusSrcMaster = kBusSrcPlayback + 2;                 // 2 rows
constexpr int kBusSrcCount = kBusSrcMaster + 2;                    // 30
}
