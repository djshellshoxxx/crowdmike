#include "TestHarness.h"
#include "Core/StateIO.h"
#include "PluginProcessor.h"

using namespace crowdmike;

CM_TEST(state_defaults_sparse_legacy_state_resets_missing_parameters) {
    auto processor = std::make_unique<CrowdMikeAudioProcessor>();
    auto& parameters = processor->getParameters();
    const auto setActual = [&](const char* id, float value) {
        auto* parameter = parameters.getParameter(id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    setActual("master.levelDb", -20.0f);
    setActual("fx.reverb.decay", 17.0f);
    setActual("fx.multi.voices", 16.0f);
    setActual("tempo.manualBpm", 240.0f);

    juce::XmlElement legacy("CrowdMikeState");
    legacy.setAttribute("schemaVersion", 1);
    auto* stored = legacy.createNewChildElement("PARAM");
    stored->setAttribute("id", "input1.trimDb");
    stored->setAttribute("value", 7.5);
    auto* unknown = legacy.createNewChildElement("PARAM");
    unknown->setAttribute("id", "future.unknown");
    unknown->setAttribute("value", 99.0);
    auto* direct = legacy.createNewChildElement("RoutingMatrix");
    direct->setAttribute("r1_2", 0.35);

    CM_EXPECT(StateIO::restore({parameters, processor->getRouting(), processor->getShow(), processor->getScenes()},
                              legacy, 2, 2) == StateIO::Result::Ok);
    CM_EXPECT_NEAR(parameters.getRawParameterValue("input1.trimDb")->load(), 7.5, 0.05);
    CM_EXPECT_NEAR(parameters.getRawParameterValue("master.levelDb")->load(), 0.0, 0.01);
    CM_EXPECT_NEAR(parameters.getRawParameterValue("fx.reverb.decay")->load(), 2.5, 0.01);
    CM_EXPECT_NEAR(parameters.getRawParameterValue("fx.multi.voices")->load(), 8.0, 0.01);
    CM_EXPECT_NEAR(parameters.getRawParameterValue("tempo.manualBpm")->load(), 120.0, 0.01);
    CM_EXPECT_NEAR(processor->getRouting().getDirect(0, 1), 0.35, 0.001);
}
