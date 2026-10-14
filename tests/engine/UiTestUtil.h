#pragma once
// Helpers for driving editor pages in tests: find components by componentID / type, write test WAV files.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace uitest {
inline juce::Component* findById(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id) return &root;
    for (auto* c : root.getChildren())
        if (auto* f = findById(*c, id)) return f;
    return nullptr;
}
template <typename T>
inline T* findFirst(juce::Component& root) {
    if (auto* t = dynamic_cast<T*>(&root)) return t;
    for (auto* c : root.getChildren())
        if (auto* f = findFirst<T>(*c)) return f;
    return nullptr;
}
inline void click(juce::Component* c) {
    if (auto* b = dynamic_cast<juce::Button*>(c); b != nullptr && b->onClick) {
        if (b->getClickingTogglesState()) b->setToggleState(!b->getToggleState(), juce::dontSendNotification);
        b->onClick();
    }
}
inline bool writeSineWav(const juce::File& f, double sr, int frames, int channels = 1, float amp = 0.5f) {
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream>(f);
    auto w = juce::WavAudioFormat().createWriterFor(os, juce::AudioFormatWriterOptions {}.withSampleRate(sr).withNumChannels(channels).withBitsPerSample(24));
    if (w == nullptr) return false;
    juce::AudioBuffer<float> b(channels, frames);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < frames; ++i) b.setSample(ch, i, amp * std::sin(0.03f * static_cast<float>(i)));
    return w->writeFromAudioSampleBuffer(b, 0, frames);
}
}
