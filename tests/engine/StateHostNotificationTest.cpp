#include "TestHarness.h"
#include "PluginProcessor.h"

using namespace crowdmike;

namespace {
struct StateHostListener final : juce::AudioProcessorListener {
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override {
        if (details.programChanged) ++restores;
    }
    int restores = 0;
};
}

CM_TEST(state_host_successful_restore_announces_changed_parameter_values) {
    auto source = std::make_unique<CrowdMikeAudioProcessor>();
    auto* level = source->getParameters().getParameter("master.levelDb");
    level->setValueNotifyingHost(level->convertTo0to1(-6.0f));
    juce::MemoryBlock state;
    source->getStateInformation(state);

    auto target = std::make_unique<CrowdMikeAudioProcessor>();
    StateHostListener host;
    target->addListener(&host);
    target->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CM_EXPECT_NEAR(target->getParameters().getRawParameterValue("master.levelDb")->load(), -6.0, 0.01);
    CM_EXPECT(host.restores == 1);
    target->removeListener(&host);
}

CM_TEST(state_host_rejected_state_does_not_announce_changed_parameter_values) {
    auto target = std::make_unique<CrowdMikeAudioProcessor>();
    StateHostListener host;
    target->addListener(&host);
    const unsigned char malformed[8] {};
    target->setStateInformation(malformed, sizeof(malformed));

    juce::XmlElement future("CrowdMikeState");
    future.setAttribute("schemaVersion", 99);
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(future, state);
    target->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CM_EXPECT(host.restores == 0);
    target->removeListener(&host);
}
