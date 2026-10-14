#pragma once
// Dedicated Reverse Chunk display (GUI_SPEC section 10): a horizontal map of the circular capture buffer showing the
// incoming write head, the stored (frozen) chunk, the backward play head, the region a trigger would capture now,
// crossfade zones, pending-trigger and state text and the wet amount. Fed by CrowdEngine::getReverseViz() through
// the shared GUI ticker; positions are normalised to the buffer capacity.
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "DSP/ReverseChunk.h"
#include "UI/Ticker.h"

namespace crowdmike::ui {
class ReverseVizView final : public juce::Component, public juce::SettableTooltipClient, private Tickable {
public:
    ReverseVizView();
    std::function<dsp::ReverseChunk::Viz()> source;     // engine snapshot
    std::function<float()> chunkSeconds;                 // current ChunkSec parameter (for the "next capture" preview)
    std::function<bool()> active;                        // false: skip polling (page hidden)
    float capacitySeconds = static_cast<float>(2.0 * dsp::ReverseChunk::kMaxChunkSec + 0.5);

    void setViz(const dsp::ReverseChunk::Viz& v) { viz = v; repaint(); }
    const dsp::ReverseChunk::Viz& getViz() const noexcept { return viz; }
    static juce::String stateName(int state, bool pending);

    void paint(juce::Graphics&) override;
    void tick() override;
private:
    dsp::ReverseChunk::Viz viz {};
    float lastChunk = -1.0f;
};
}
