#pragma once
// Planar float storage allocated once in prepare(); gives stable channel pointers for the audio thread.
#include <algorithm>
#include <vector>

namespace crowdmike {
class EngineBuffer {
public:
    void allocate(int channels, int frames) {
        numCh = channels;
        numFrames = frames;
        storage.assign(static_cast<size_t>(std::max(1, channels)) * static_cast<size_t>(std::max(1, frames)), 0.0f);
        pointers.resize(static_cast<size_t>(std::max(1, channels)));
        for (int c = 0; c < channels; ++c) pointers[static_cast<size_t>(c)] = storage.data() + static_cast<size_t>(c) * static_cast<size_t>(frames);
    }
    float* channel(int c) noexcept { return pointers[static_cast<size_t>(c)]; }
    const float* channel(int c) const noexcept { return pointers[static_cast<size_t>(c)]; }
    float* const* data() noexcept { return pointers.data(); }
    float* const* data(int firstChannel) noexcept { return pointers.data() + firstChannel; }
    int channels() const noexcept { return numCh; }
    int frames() const noexcept { return numFrames; }
    void clear(int n) noexcept { for (int c = 0; c < numCh; ++c) std::fill(pointers[static_cast<size_t>(c)], pointers[static_cast<size_t>(c)] + n, 0.0f); }
    void clearChannel(int c, int n) noexcept { std::fill(pointers[static_cast<size_t>(c)], pointers[static_cast<size_t>(c)] + n, 0.0f); }
private:
    std::vector<float> storage;
    std::vector<float*> pointers;
    int numCh = 0, numFrames = 0;
};
}
