#include "OscServer.h"

namespace crowdmike {
std::vector<float> OscServer::argsToFloats(const juce::OSCMessage& m) {
    std::vector<float> out;
    for (const auto& a : m) {
        if (a.isFloat32()) out.push_back(a.getFloat32());
        else if (a.isInt32()) out.push_back(static_cast<float>(a.getInt32()));
        else if (a.isString()) {
            const auto s = a.getString();
            if (s.containsOnly("0123456789.-+eE") && s.isNotEmpty()) out.push_back(s.getFloatValue());
        }
    }
    return out;
}

bool OscServer::start(const OscSettings& s) {
    stop();
    lastError = {};
    socket = std::make_unique<juce::DatagramSocket>(false);
    const juce::String bindTo = s.bindAddress.isNotEmpty() ? s.bindAddress : (s.localhostOnly ? juce::String("127.0.0.1") : juce::String());
    if (!socket->bindToPort(s.port, bindTo)) {
        lastError = "Cannot bind OSC port " + juce::String(s.port) + (bindTo.isNotEmpty() ? " on " + bindTo : juce::String());
        socket.reset();
        return false;
    }
    if (!receiver.connectToSocket(*socket)) {
        lastError = "OSC receiver could not use the socket";
        socket.reset();
        return false;
    }
    receiver.addListener(this);
    running = true;
    return true;
}

void OscServer::stop() {
    if (!running && socket == nullptr) return;
    receiver.removeListener(this);
    receiver.disconnect();
    socket.reset();
    running = false;
}

void OscServer::oscMessageReceived(const juce::OSCMessage& m) {
    ++messagesReceived;
    hub.handleOsc(m.getAddressPattern().toString(), argsToFloats(m));
}

void OscServer::oscBundleReceived(const juce::OSCBundle& b) {
    for (const auto& element : b) {
        if (element.isMessage()) oscMessageReceived(element.getMessage());
        else if (element.isBundle()) oscBundleReceived(element.getBundle());
    }
}
}
