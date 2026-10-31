#include "TestHarness.h"
#include "PluginProcessor.h"
#include <atomic>
#include <thread>

using namespace crowdmike;

CM_TEST(macro_snapshot_concurrent_publication_keeps_complete_mapping) {
    auto processor = std::make_unique<CrowdMikeAudioProcessor>();
    auto& bank = processor->getBank();
    MacroSet forward, reverse;
    for (auto* set : {&forward, &reverse}) {
        auto& map = set->macros[0];
        map.count = MacroMap::kMaxDests;
        for (int i = 0; i < MacroMap::kMaxDests; ++i) {
            auto& destination = map.dests[static_cast<size_t>(i)];
            destination.paramIndex = ParamBank::index(Mod::Input, i, params::InputP::Trim);
            destination.min01 = set == &forward ? 0.0f : 1.0f;
            destination.max01 = destination.min01;
        }
    }
    bank.publishMacros(forward);
    std::atomic<bool> start {false};
    std::thread publisher([&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int i = 0; i < 100000; ++i) bank.publishMacros(i % 2 == 0 ? reverse : forward);
    });
    start.store(true, std::memory_order_release);
    bool coherent = true;
    for (int i = 0; i < 10000; ++i) {
        bank.refresh();
        const float first = bank.effectiveAt(ParamBank::index(Mod::Input, 0, params::InputP::Trim));
        // One complete mapping sets all input trims to -60 dB, the other to
        // +24 dB. A callback must never mix assignments from those mappings.
        coherent = coherent && (first == -60.0f || first == 24.0f);
        for (int input = 1; input < kMaxInputs; ++input)
            coherent = coherent && bank.effectiveAt(ParamBank::index(Mod::Input, input, params::InputP::Trim)) == first;
    }
    publisher.join();
    CM_EXPECT(coherent);
    CM_EXPECT(bank.currentMacros().macros[0].dests[0].min01 == 0.0f);
}
