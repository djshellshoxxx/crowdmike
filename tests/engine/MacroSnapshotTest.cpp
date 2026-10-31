#include "TestHarness.h"
#include "PluginProcessor.h"
#include <array>
#include <atomic>
#include <thread>

using namespace crowdmike;

CM_TEST(macro_snapshot_concurrent_publication_keeps_complete_mapping) {
    auto processor = std::make_unique<CrowdMikeAudioProcessor>();
    auto& bank = processor->getBank();
    std::array<float, kMaxInputs> expectedLow {}, expectedHigh {};
    for (int input = 0; input < kMaxInputs; ++input) {
        auto* parameter = bank.parameterAt(ParamBank::index(Mod::Input, input, params::InputP::Trim));
        expectedLow[static_cast<size_t>(input)] = parameter->convertFrom0to1(0.0f);
        expectedHigh[static_cast<size_t>(input)] = parameter->convertFrom0to1(1.0f);
    }
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
    bool validEndpoints = true;
    bool coherentMapping = true;
    for (int i = 0; i < 10000; ++i) {
        bank.refresh();
        const float first = bank.effectiveAt(ParamBank::index(Mod::Input, 0, params::InputP::Trim));
        // JUCE's interval snapping may round an endpoint differently when the
        // compiler fuses multiply-adds. Snapshot membership and channel
        // coherence must still match the public parameter conversion exactly.
        for (int input = 0; input < kMaxInputs; ++input) {
            const float actual = bank.effectiveAt(ParamBank::index(Mod::Input, input, params::InputP::Trim));
            validEndpoints = validEndpoints && (actual == expectedLow[static_cast<size_t>(input)]
                                                 || actual == expectedHigh[static_cast<size_t>(input)]);
            coherentMapping = coherentMapping && actual == first;
        }
    }
    publisher.join();
    CM_EXPECT(validEndpoints);
    CM_EXPECT(coherentMapping);
    CM_EXPECT(bank.currentMacros().macros[0].dests[0].min01 == 0.0f);
}
