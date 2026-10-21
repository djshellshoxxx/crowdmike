#include "StateIO.h"
#include "Routing/RoutingState.h"

#ifndef CROWDMIKE_VERSION_STRING
 #define CROWDMIKE_VERSION_STRING "0.3.0"
#endif

namespace crowdmike::StateIO {
const char* const kStateType = "CrowdMikeState";

namespace {
void migrateV1ToV2(juce::XmlElement& xml) {
    // v1 stored the direct input->output matrix as attributes r<in>_<out> on <RoutingMatrix>.
    RoutingState routing;
    bool hadRouting = false;
    if (auto* old = xml.getChildByName("RoutingMatrix")) {
        for (int out = 0; out < kMaxOutputs; ++out)
            for (int in = 0; in < kMaxInputs; ++in) {
                const auto key = "r" + juce::String(in + 1) + "_" + juce::String(out + 1);
                const double raw = old->getDoubleAttribute(key, 0.0);
                routing.setDirect(in, out, std::isfinite(raw) ? static_cast<float>(juce::jlimit(-2.0, 2.0, raw)) : 0.0f);
            }
        routing.setHasSavedDirect(true);
        hadRouting = true;
        xml.removeChildElement(old, true);
    }
    if (hadRouting) {
        if (auto tree = routing.toValueTree(); tree.isValid())
            if (auto routingXml = tree.createXml()) xml.addChildElement(routingXml.release());
    }
    xml.setAttribute("schemaVersion", 2);
}
}

int migrateToCurrent(juce::XmlElement& xml) {
    if (!xml.hasTagName(kStateType)) return -1;
    int version = xml.getIntAttribute("schemaVersion", -1);
    if (version < 1) return -1;
    const int found = version;
    if (version == 1) { migrateV1ToV2(xml); version = 2; }
    return found;
}

std::unique_ptr<juce::XmlElement> serialize(Parts p, const EngineConfigInfo& engine) {
    auto state = p.apvts.copyState();
    auto xml = state.createXml();
    if (xml == nullptr) return nullptr;
    xml->setAttribute("schemaVersion", kSchemaVersion);
    xml->setAttribute("appVersion", CROWDMIKE_VERSION_STRING);
    xml->setAttribute("platform", juce::SystemStats::getOperatingSystemName());
    if (auto r = p.routing.toValueTree().createXml()) xml->addChildElement(r.release());
    if (auto s = p.show.toValueTree().createXml()) xml->addChildElement(s.release());
    if (auto sc = p.scenes.toValueTree().createXml()) xml->addChildElement(sc.release());
    auto* cfg = xml->createNewChildElement("EngineConfig");
    cfg->setAttribute("sampleRate", engine.sampleRate);
    cfg->setAttribute("blockSize", engine.blockSize);
    cfg->setAttribute("inputs", engine.inputs);
    cfg->setAttribute("outputs", engine.outputs);
    return xml;
}

Result restore(Parts p, const juce::XmlElement& source, int activeInputs, int activeOutputs) {
    juce::XmlElement xml(source);
    const int found = xml.hasTagName(kStateType) ? xml.getIntAttribute("schemaVersion", -1) : -1;
    if (found < 1) return Result::Invalid;
    if (found > kSchemaVersion) return Result::NewerThanSupported;
    migrateToCurrent(xml);

    if (auto* r = xml.getChildByName("Routing")) {
        auto tree = juce::ValueTree::fromXml(*r);
        if (!p.routing.fromValueTree(tree)) p.routing.resetDirectDefault(activeInputs, activeOutputs);
    } else {
        // Parameter-only state: keep the first-release behaviour of re-deriving the default from the host layout.
        p.routing.clearAll();
        p.routing.setHasSavedDirect(false);
        p.routing.resetDirectDefault(activeInputs, activeOutputs);
    }
    if (auto* s = xml.getChildByName("Show")) p.show.fromValueTree(juce::ValueTree::fromXml(*s));
    else p.show.resetToDefaults();
    if (auto* sc = xml.getChildByName("Scenes")) p.scenes.fromValueTree(juce::ValueTree::fromXml(*sc));

    auto parameterState = juce::ValueTree::fromXml(xml);
    for (int i = parameterState.getNumChildren() - 1; i >= 0; --i) {
        const auto type = parameterState.getChild(i).getType().toString();
        if (type != "PARAM") parameterState.removeChild(i, nullptr);
    }
    p.apvts.replaceState(parameterState);
    return Result::Ok;
}
}
