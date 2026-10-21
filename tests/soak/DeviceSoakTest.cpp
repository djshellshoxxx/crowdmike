// Device-disconnect simulation: the host/standalone device layer tears the stream down and restarts it with a
// different rate, block size and channel layout (what an interface unplug/replug or a driver switch looks like to
// the engine). Also exercises state save/restore across the restart.
#include "SoakSupport.h"

using namespace soak;

CM_TEST(Soak_DeviceDisconnectReconnectCycles) {
    struct Cfg { int in, out; double sr; int block; };
    const Cfg cfgs[] = { { 16, 16, 48000.0, 256 }, { 2, 2, 44100.0, 512 }, { 8, 4, 96000.0, 128 },
                         { 16, 16, 96000.0, 2048 }, { 1, 2, 48000.0, 64 }, { 4, 8, 192000.0, 1024 } };
    CrowdMikeAudioProcessor p;
    Source src;
    juce::MidiBuffer midi;
    const int cycles = (int) envSeconds("CROWDMIKE_SOAK_DEVICE_CYCLES", 300.0);
    std::int64_t baseline = -1;
    bool finite = true;
    std::uint64_t audioAllocs = 0;
    for (int c = 0; c < cycles; ++c) {
        const auto& cfg = cfgs[(std::size_t) c % (sizeof(cfgs) / sizeof(Cfg))];
        CM_EXPECT(setLayout(p, cfg.in, cfg.out));
        p.prepareToPlay(cfg.sr, cfg.block);
        if (c % 3 == 0) enableEverything(p, cfg.in);
        juce::AudioBuffer<float> buf(std::max(cfg.in, cfg.out), cfg.block);
        // "stream running": ~0.5 s of audio, with a dead gap (no callbacks) before the next restart
        const int blocks = std::max(4, (int) (0.5 * cfg.sr / cfg.block));
        for (int b = 0; b < blocks; ++b) {
            src.fill(buf, cfg.sr);
            const auto a0 = threadAllocCount();
            p.processBlock(buf, midi);
            if (c >= 12) audioAllocs += threadAllocCount() - a0;
            finite = finite && scan(buf).finite;
        }
        // state must survive the reconnect
        juce::MemoryBlock state;
        p.getStateInformation(state);
        p.setStateInformation(state.getData(), (int) state.getSize());
        p.releaseResources();
        if (c == 12) baseline = globalAllocStats().outstanding();
    }
    CM_EXPECT(finite);
    CM_EXPECT(audioAllocs == 0);
    CM_EXPECT(baseline >= 0);
    // Reconnect cycles must not accumulate engine objects (allow a small constant for allocator/caches).
    CM_EXPECT(globalAllocStats().outstanding() - baseline <= 256);
}
