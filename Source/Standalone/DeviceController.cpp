#include "DeviceController.h"

namespace crowdmike {
namespace {
class SettingsWindow final : public juce::DocumentWindow {
public:
    SettingsWindow(juce::AudioDeviceManager& dm, std::function<void()> onClose)
        : juce::DocumentWindow("Audio / MIDI Device Settings", juce::Colours::black, juce::DocumentWindow::closeButton),
          closeCallback(std::move(onClose)) {
        setUsingNativeTitleBar(true);
        auto* selector = new juce::AudioDeviceSelectorComponent(dm, 0, kMaxInputs, 0, kMaxOutputs, true, false, false, false);
        selector->setSize(560, 520);
        setContentOwned(selector, true);
        centreWithSize(getWidth(), getHeight());
        setResizable(false, false);
        setVisible(true);
    }
    void closeButtonPressed() override { if (closeCallback) closeCallback(); }
private:
    std::function<void()> closeCallback;
};
}

DeviceController::DeviceController(CrowdMikeAudioProcessor& p, const juce::File& f) : processor(p), folder(f) {
    folder.createDirectory();
    for (auto& m : mapSlots) { m.inputSource.fill(-1); m.outputTarget.fill(-1); }
}

DeviceController::~DeviceController() {
    stopTimer();
    processor.setHostServices(nullptr);   // the processor outlives us and exports the Show Report from its destructor
    deviceManager.removeChangeListener(this);
    deviceManager.removeAudioCallback(this);
    deviceManager.removeMidiInputDeviceCallback({}, &midiCollector);
}

int DeviceController::resolveChannel(const juce::StringArray& active, const juce::String& name) {
    return name.isEmpty() ? -1 : active.indexOf(name);
}

juce::String DeviceController::initialise() {
    std::unique_ptr<juce::XmlElement> saved;
    if (auto xml = juce::XmlDocument::parse(folder.getChildFile("device.xml"))) saved = std::move(xml);
    loadBindings();
    const auto err = deviceManager.initialise(kMaxInputs, kMaxOutputs, saved.get(), true);
    deviceManager.addChangeListener(this);
    deviceManager.addAudioCallback(this);
    deviceManager.addMidiInputDeviceCallback({}, &midiCollector);
    processor.setHostServices(this);
    startTimer(1000);
    return err;
}

void DeviceController::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    deviceRunning.store(true);
    deviceLost.store(false);
    const double sr = device->getCurrentSampleRate();
    const int block = device->getCurrentBufferSizeSamples();
    midiCollector.reset(sr);
    const int devIns = device->getActiveInputChannels().countNumberOfSetBits();
    const int devOuts = device->getActiveOutputChannels().countNumberOfSetBits();
    // Fill sensible first-run bindings, then size the engine to the highest bound channel.
    const auto inNames = device->getInputChannelNames();
    const auto outNames = device->getOutputChannelNames();
    juce::StringArray activeIn, activeOut;
    for (int i = 0; i < inNames.size(); ++i) if (device->getActiveInputChannels()[i]) activeIn.add(inNames[i]);
    for (int i = 0; i < outNames.size(); ++i) if (device->getActiveOutputChannels()[i]) activeOut.add(outNames[i]);
    for (int i = 0; i < kMaxInputs; ++i)
        if (inputBindingNames[static_cast<size_t>(i)].isEmpty() && i < activeIn.size()) inputBindingNames[static_cast<size_t>(i)] = activeIn[i];
    for (int i = 0; i < kMaxOutputs; ++i)
        if (outputBindingNames[static_cast<size_t>(i)].isEmpty() && i < activeOut.size()) outputBindingNames[static_cast<size_t>(i)] = activeOut[i];
    engineIns = juce::jlimit(1, kMaxInputs, juce::jmax(1, juce::jmin(devIns, kMaxInputs)));
    engineOuts = juce::jlimit(1, kMaxOutputs, juce::jmax(1, juce::jmin(devOuts, kMaxOutputs)));
    applyProcessorLayout(engineIns, engineOuts, sr, block);
    rebuildMaps();
    saveBindings();
}

void DeviceController::applyProcessorLayout(int ins, int outs, double sr, int block) {
    processor.releaseResources();
    auto layout = processor.getBusesLayout();
    layout.getChannelSet(true, 0) = juce::AudioChannelSet::discreteChannels(ins);
    layout.getChannelSet(false, 0) = juce::AudioChannelSet::discreteChannels(outs);
    processor.setBusesLayout(layout);
    processor.setRateAndBufferSizeDetails(sr, block);
    workBuffer.setSize(juce::jmax(ins, outs), juce::jmax(block, 4096), false, true, false);
    processor.prepareToPlay(sr, juce::jmax(block, 4096));
}

void DeviceController::rebuildMaps() {
    auto* device = deviceManager.getCurrentAudioDevice();
    const int next = 1 - activeMap.load();
    auto& m = mapSlots[static_cast<size_t>(next)];
    m.inputSource.fill(-1);
    m.outputTarget.fill(-1);
    if (device != nullptr) {
        juce::StringArray activeIn, activeOut;
        const auto inNames = device->getInputChannelNames();
        const auto outNames = device->getOutputChannelNames();
        for (int i = 0; i < inNames.size(); ++i) if (device->getActiveInputChannels()[i]) activeIn.add(inNames[i]);
        for (int i = 0; i < outNames.size(); ++i) if (device->getActiveOutputChannels()[i]) activeOut.add(outNames[i]);
        for (int i = 0; i < engineIns; ++i) m.inputSource[static_cast<size_t>(i)] = resolveChannel(activeIn, inputBindingNames[static_cast<size_t>(i)]);
        for (int o = 0; o < engineOuts; ++o) m.outputTarget[static_cast<size_t>(o)] = resolveChannel(activeOut, outputBindingNames[static_cast<size_t>(o)]);
    }
    activeMap.store(next);
}

void DeviceController::audioDeviceStopped() { deviceRunning.store(false); }

void DeviceController::audioDeviceError(const juce::String& message) {
    lastDeviceError = message;
    deviceLost.store(true);
}

void DeviceController::audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut,
                                                        int numSamples, const juce::AudioIODeviceCallbackContext&) {
    const auto start = juce::Time::getHighResolutionTicks();
    const auto& maps = mapSlots[static_cast<size_t>(activeMap.load(std::memory_order_acquire))];
    const int chans = juce::jmax(engineIns, engineOuts);
    for (int off = 0; off < numSamples;) {
        const int n = juce::jmin(numSamples - off, workBuffer.getNumSamples());
        juce::AudioBuffer<float> view(workBuffer.getArrayOfWritePointers(), chans, n);
        view.clear();
        for (int i = 0; i < engineIns; ++i) {
            const int src = maps.inputSource[static_cast<size_t>(i)];
            if (src >= 0 && src < numIn && in[src] != nullptr) view.copyFrom(i, 0, in[src] + off, n);
        }
        midiBuffer.clear();
        midiCollector.removeNextBlockOfMessages(midiBuffer, n);
        processor.processBlock(view, midiBuffer);
        for (int o = 0; o < numOut; ++o) if (out[o] != nullptr) juce::FloatVectorOperations::clear(out[o] + off, n);
        for (int o = 0; o < engineOuts; ++o) {
            const int dst = maps.outputTarget[static_cast<size_t>(o)];
            if (dst >= 0 && dst < numOut && out[dst] != nullptr) juce::FloatVectorOperations::copy(out[dst] + off, view.getReadPointer(o), n);
        }
        off += n;
    }
    if (auto* device = deviceManager.getCurrentAudioDevice()) {
        const double used = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
        const double budget = static_cast<double>(numSamples) / juce::jmax(1.0, device->getCurrentSampleRate());
        smoothedCpu.store(smoothedCpu.load() * 0.9f + static_cast<float>(used / budget) * 0.1f, std::memory_order_relaxed);
    }
}

void DeviceController::changeListenerCallback(juce::ChangeBroadcaster*) {
    if (deviceManager.getCurrentAudioDevice() == nullptr) deviceLost.store(true);
    else { deviceLost.store(false); rebuildMaps(); }
    saveDeviceState();
}

void DeviceController::timerCallback() {
    // Device disconnect/reconnect (TESTING_SPEC section 11): audio continues silently; retry opening every few seconds.
    if (deviceManager.getCurrentAudioDevice() == nullptr || deviceLost.load()) {
        if (++reopenCounter >= 3) {
            reopenCounter = 0;
            if (auto xml = deviceManager.createStateXml())
                deviceManager.initialise(kMaxInputs, kMaxOutputs, xml.get(), true);
            else
                deviceManager.initialiseWithDefaultDevices(kMaxInputs, kMaxOutputs);
        }
    }
    // Autosave every ~30 s (CONTROL_SPEC section 9), off the audio thread.
    if (++autosaveCounter >= 30) { autosaveCounter = 0; saveSessionNow(); }
}

void DeviceController::saveDeviceState() {
    if (auto xml = deviceManager.createStateXml()) xml->writeTo(folder.getChildFile("device.xml"));
}

DeviceStatus DeviceController::getDeviceStatus() {
    DeviceStatus s;
    if (auto* d = deviceManager.getCurrentAudioDevice()) {
        s.typeName = d->getTypeName();
        s.deviceName = d->getName();
        s.sampleRate = d->getCurrentSampleRate();
        s.bufferSize = d->getCurrentBufferSizeSamples();
        s.inputLatencySamples = d->getInputLatencyInSamples();
        s.outputLatencySamples = d->getOutputLatencyInSamples();
        s.numDeviceInputs = d->getInputChannelNames().size();
        s.numDeviceOutputs = d->getOutputChannelNames().size();
        s.xruns = d->getXRunCount();
        s.deviceOpen = true;
    }
    s.cpuLoad = static_cast<float>(deviceManager.getCpuUsage());
    s.deviceLost = deviceLost.load();
    s.error = lastDeviceError;
    return s;
}

void DeviceController::showDeviceSettings() {
    if (settingsWindow != nullptr) { settingsWindow->toFront(true); return; }
    settingsWindow = std::make_unique<SettingsWindow>(deviceManager, [this] { settingsWindow.reset(); saveDeviceState(); });
}

juce::StringArray DeviceController::getInputChannelNames() {
    juce::StringArray r;
    if (auto* d = deviceManager.getCurrentAudioDevice()) {
        const auto names = d->getInputChannelNames();
        for (int i = 0; i < names.size(); ++i) if (d->getActiveInputChannels()[i]) r.add(names[i]);
    }
    return r;
}
juce::StringArray DeviceController::getOutputChannelNames() {
    juce::StringArray r;
    if (auto* d = deviceManager.getCurrentAudioDevice()) {
        const auto names = d->getOutputChannelNames();
        for (int i = 0; i < names.size(); ++i) if (d->getActiveOutputChannels()[i]) r.add(names[i]);
    }
    return r;
}

std::vector<DeviceBinding> DeviceController::getInputBindings() {
    std::vector<DeviceBinding> v;
    const auto active = getInputChannelNames();
    for (int i = 0; i < engineIns; ++i) {
        DeviceBinding b;
        b.engineChannel = i;
        b.physicalName = inputBindingNames[static_cast<size_t>(i)];
        b.physicalIndex = resolveChannel(active, b.physicalName);
        b.resolved = b.physicalIndex >= 0;
        v.push_back(b);
    }
    return v;
}
std::vector<DeviceBinding> DeviceController::getOutputBindings() {
    std::vector<DeviceBinding> v;
    const auto active = getOutputChannelNames();
    for (int i = 0; i < engineOuts; ++i) {
        DeviceBinding b;
        b.engineChannel = i;
        b.physicalName = outputBindingNames[static_cast<size_t>(i)];
        b.physicalIndex = resolveChannel(active, b.physicalName);
        b.resolved = b.physicalIndex >= 0;
        v.push_back(b);
    }
    return v;
}

void DeviceController::setInputBinding(int ch, const juce::String& name) {
    if (ch < 0 || ch >= kMaxInputs) return;
    inputBindingNames[static_cast<size_t>(ch)] = name;
    rebuildMaps();
    saveBindings();
}
void DeviceController::setOutputBinding(int ch, const juce::String& name) {
    if (ch < 0 || ch >= kMaxOutputs) return;
    outputBindingNames[static_cast<size_t>(ch)] = name;
    rebuildMaps();
    saveBindings();
}

void DeviceController::loadBindings() {
    if (auto xml = juce::XmlDocument::parse(folder.getChildFile("bindings.xml"))) {
        for (int i = 0; i < kMaxInputs; ++i) inputBindingNames[static_cast<size_t>(i)] = xml->getStringAttribute("in" + juce::String(i));
        for (int o = 0; o < kMaxOutputs; ++o) outputBindingNames[static_cast<size_t>(o)] = xml->getStringAttribute("out" + juce::String(o));
    }
}
void DeviceController::saveBindings() const {
    juce::XmlElement xml("Bindings");
    for (int i = 0; i < kMaxInputs; ++i) xml.setAttribute("in" + juce::String(i), inputBindingNames[static_cast<size_t>(i)]);
    for (int o = 0; o < kMaxOutputs; ++o) xml.setAttribute("out" + juce::String(o), outputBindingNames[static_cast<size_t>(o)]);
    xml.writeTo(folder.getChildFile("bindings.xml"));
}

juce::StringArray DeviceController::getMidiInputNames() {
    juce::StringArray names;
    for (const auto& d : juce::MidiInput::getAvailableDevices()) names.add(d.name);
    return names;
}
bool DeviceController::isMidiInputEnabled(const juce::String& name) {
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        if (d.name == name) return deviceManager.isMidiInputDeviceEnabled(d.identifier);
    return false;
}
void DeviceController::setMidiInputEnabled(const juce::String& name, bool enabled) {
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        if (d.name == name) deviceManager.setMidiInputDeviceEnabled(d.identifier, enabled);
}

void DeviceController::saveSessionNow() {
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    getAutosaveFile().replaceWithData(state.getData(), state.getSize());
}

void DeviceController::loadLastSession() {
    const auto flag = folder.getChildFile("running.flag");
    const auto autosave = getAutosaveFile();
    crashRecovered = flag.existsAsFile() && autosave.existsAsFile();
    juce::MemoryBlock block;
    const auto last = folder.getChildFile("lastsession.cmstate");
    const juce::File& source = crashRecovered ? autosave : last;
    if (source.existsAsFile() && source.loadFileAsData(block) && block.getSize() > 0)
        processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    flag.replaceWithText(juce::Time::getCurrentTime().toISO8601(true));
}

void DeviceController::shutdownCleanly() {
    stopTimer();
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    folder.getChildFile("lastsession.cmstate").replaceWithData(state.getData(), state.getSize());
    saveDeviceState();
    folder.getChildFile("running.flag").deleteFile();
}
}
