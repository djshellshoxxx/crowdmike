#pragma once
// Standalone OSC receiver (CONTROL_SPEC section 6): optional, bindable to localhost or a chosen interface.
#include <juce_osc/juce_osc.h>
#include <memory>
#include "Control/ControlHub.h"

namespace crowdmike {
class OscServer final : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback> {
public:
    explicit OscServer(ControlHub& h) : hub(h) {}
    ~OscServer() override { stop(); }
    bool start(const OscSettings&);     // false + lastError on failure (port in use, bad address)
    void stop();
    bool isRunning() const noexcept { return running; }
    juce::String lastError;
    int messagesReceived = 0;

    // Converts an OSC message to floats (ints, floats, bools; strings ignored). Public for tests.
    static std::vector<float> argsToFloats(const juce::OSCMessage&);

private:
    void oscMessageReceived(const juce::OSCMessage&) override;
    void oscBundleReceived(const juce::OSCBundle&) override;
    ControlHub& hub;
    juce::OSCReceiver receiver;
    std::unique_ptr<juce::DatagramSocket> socket;
    bool running = false;
};
}
