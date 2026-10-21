#pragma once
// Standalone audio/MIDI device ownership (IO_SPEC sections 1, 3, 8). Owns the AudioDeviceManager, maps physical device
// channels to engine channels through machine-local bindings (never stored in portable scenes), survives device removal
// and keeps the audio callback allocation-free.
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
#include "PluginProcessor.h"
#include "Standalone/HostServices.h"

namespace crowdmike {
class DeviceController final : public HostServices,
                               public juce::AudioIODeviceCallback,
                               private juce::ChangeListener,
                               private juce::Timer {
public:
    DeviceController(CrowdMikeAudioProcessor&, const juce::File& settingsFolder);
    ~DeviceController() override;

    // Opens the saved (or default) device. Returns an empty string on success.
    juce::String initialise();
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    // ---- HostServices
    DeviceStatus getDeviceStatus() override;
    void showDeviceSettings() override;
    juce::StringArray getInputChannelNames() override;
    juce::StringArray getOutputChannelNames() override;
    std::vector<DeviceBinding> getInputBindings() override;
    std::vector<DeviceBinding> getOutputBindings() override;
    void setInputBinding(int engineChannel, const juce::String& physicalName) override;
    void setOutputBinding(int engineChannel, const juce::String& physicalName) override;
    void saveSessionNow() override;
    bool restoredAfterCrash() override { return crashRecovered; }
    juce::File getAutosaveFile() override { return folder.getChildFile("autosave.cmstate"); }
    juce::StringArray getMidiInputNames() override;
    void setMidiInputEnabled(const juce::String& name, bool enabled) override;
    bool isMidiInputEnabled(const juce::String& name) override;

    // ---- session persistence
    void loadLastSession();        // restores crash-recovery snapshot if the previous run did not exit cleanly
    void shutdownCleanly();        // saves last session, removes the running flag

    // ---- AudioIODeviceCallback
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceError(const juce::String& message) override;

    // Pure mapping helper (unit-tested): resolve a binding name against the device's active channel names.
    static int resolveChannel(const juce::StringArray& activeChannelNames, const juce::String& name);

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void rebuildMaps();
    void applyProcessorLayout(int engineIns, int engineOuts, double sr, int block);
    void loadBindings();
    void saveBindings() const;
    void saveDeviceState();

    CrowdMikeAudioProcessor& processor;
    juce::File folder;
    juce::AudioDeviceManager deviceManager;
    juce::MidiMessageCollector midiCollector;

    // Active-channel resolution (rebuilt on the message thread, published by swapping an index into two tables).
    struct Maps {
        std::array<int, kMaxInputs> inputSource;     // engine input -> index in the callback's input array (-1 = none)
        std::array<int, kMaxOutputs> outputTarget;   // engine output -> index in the callback's output array
    };
    std::array<Maps, 2> mapSlots {};
    std::atomic<int> activeMap { 0 };

    std::array<juce::String, kMaxInputs> inputBindingNames;
    std::array<juce::String, kMaxOutputs> outputBindingNames;
    int engineIns = 2, engineOuts = 2;
    std::atomic<bool> deviceRunning { false }, deviceLost { false };
    bool crashRecovered = false;
    int autosaveCounter = 0;
    int reopenCounter = 0;
    juce::AudioBuffer<float> workBuffer;
    juce::MidiBuffer midiBuffer;
    std::unique_ptr<juce::DocumentWindow> settingsWindow;
    juce::String lastDeviceError;
    std::atomic<float> smoothedCpu { 0.0f };
    juce::StringArray enabledMidi;
};
}
