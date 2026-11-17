#include "TestHarness.h"
#include "PluginProcessor.h"

using namespace crowdmike;

namespace {
void dispatchPulseTimers() {
    // The timer thread marks callbacks due; dispatch them on this message thread.
    juce::Thread::sleep(100);
    juce::Timer::callPendingTimersSynchronously();
}
}

CM_TEST(control_lifetime_active_pulse_resets_parameter) {
    juce::ScopedJuceInitialiser_GUI gui;
    auto processor = std::make_unique<CrowdMikeAudioProcessor>();
    auto& parameters = processor->getParameters();
    ControlHub hub(parameters, processor->getScenes(), processor->getShow());
    const int target = ParamBank::index(Mod::Input, 0, params::InputP::Mute);

    CM_EXPECT(hub.pulseParam(target, 1));
    CM_EXPECT_NEAR(parameters.getRawParameterValue("input1.mute")->load(), 1.0, 0.0);
    dispatchPulseTimers();
    CM_EXPECT_NEAR(parameters.getRawParameterValue("input1.mute")->load(), 0.0, 0.0);
}

CM_TEST(control_lifetime_destroyed_hub_cancels_pending_pulse) {
    juce::ScopedJuceInitialiser_GUI gui;
    auto processor = std::make_unique<CrowdMikeAudioProcessor>();
    auto& parameters = processor->getParameters();
    const int target = ParamBank::index(Mod::Input, 0, params::InputP::Mute);
    {
        ControlHub hub(parameters, processor->getScenes(), processor->getShow());
        CM_EXPECT(hub.pulseParam(target, 1));
    }

    // Keeping the parameter alive makes the stale write observable without invoking
    // undefined behaviour; a plugin teardown also destroys this parameter.
    dispatchPulseTimers();
    CM_EXPECT_NEAR(parameters.getRawParameterValue("input1.mute")->load(), 1.0, 0.0);
}
