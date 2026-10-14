#pragma once
// Narrow interface the standalone shell implements so the (format neutral) editor can show device state and open the
// device settings without depending on the application class. Null in plug-in builds.
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

namespace crowdmike {
struct DeviceBinding {
    int engineChannel = 0;          // engine input/output index
    juce::String physicalName;      // stable key (device channel name), empty = unassigned
    int physicalIndex = -1;         // resolved index into the current device, -1 = unresolved
    bool resolved = false;
};

struct DeviceStatus {
    juce::String typeName, deviceName, error;
    double sampleRate = 0.0;
    int bufferSize = 0;
    int inputLatencySamples = 0, outputLatencySamples = 0;
    int numDeviceInputs = 0, numDeviceOutputs = 0;
    int xruns = 0;
    float cpuLoad = 0.0f;
    bool deviceOpen = false;
    bool deviceLost = false;         // device disappeared; audio continues silently until it returns
};

class HostServices {
public:
    virtual ~HostServices() = default;
    virtual DeviceStatus getDeviceStatus() = 0;
    virtual void showDeviceSettings() = 0;     // may be a modal-free window
    virtual juce::StringArray getInputChannelNames() = 0;
    virtual juce::StringArray getOutputChannelNames() = 0;
    virtual std::vector<DeviceBinding> getInputBindings() = 0;
    virtual std::vector<DeviceBinding> getOutputBindings() = 0;
    virtual void setInputBinding(int engineChannel, const juce::String& physicalName) = 0;
    virtual void setOutputBinding(int engineChannel, const juce::String& physicalName) = 0;
    virtual void saveSessionNow() = 0;
    virtual bool restoredAfterCrash() = 0;
    virtual juce::File getAutosaveFile() = 0;
    virtual juce::StringArray getMidiInputNames() { return {}; }
    virtual void setMidiInputEnabled(const juce::String& /*name*/, bool /*enabled*/) {}
    virtual bool isMidiInputEnabled(const juce::String& /*name*/) { return false; }
};
}
