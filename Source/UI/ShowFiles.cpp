#include "ShowFiles.h"
#include "Core/StateIO.h"

namespace crowdmike::ui {
bool saveShowFile(CrowdMikeAudioProcessor& p, const juce::File& file, juce::String& error) {
    juce::MemoryBlock blob;
    p.getStateInformation(blob);
    if (blob.getSize() == 0) { error = "Nothing to save"; return false; }
    if (!file.replaceWithData(blob.getData(), blob.getSize())) { error = "Cannot write " + file.getFullPathName(); return false; }
    return true;
}

bool loadShowFile(CrowdMikeAudioProcessor& p, const juce::File& file, juce::String& error) {
    juce::MemoryBlock blob;
    if (!file.existsAsFile() || !file.loadFileAsData(blob) || blob.getSize() == 0) { error = "Cannot read " + file.getFullPathName(); return false; }
    const auto xml = juce::AudioProcessor::getXmlFromBinary(blob.getData(), static_cast<int>(blob.getSize()));
    if (xml == nullptr || !xml->hasTagName(StateIO::kStateType)) { error = file.getFileName() + " is not a CrowdMike show"; return false; }
    p.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    return true;
}

void resetShowToDefaults(CrowdMikeAudioProcessor& p) {
    p.getUndoManager().beginNewTransaction("Reset show to defaults");
    for (int i = 0; i < ParamBank::totalCount(); ++i)
        if (auto* param = p.getBank().parameterAt(i)) {
            if (param->getValue() == param->getDefaultValue()) continue;
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->getDefaultValue());
            param->endChangeGesture();
        }
    p.getShow().resetToDefaults();
    auto& r = p.getRouting();
    r.clearAll();
    r.resetDirectDefault(p.getActiveInputChannelCount(), p.getActiveOutputChannelCount());
    r.setHasSavedDirect(false);
    p.applyShowChanges();
}
}
