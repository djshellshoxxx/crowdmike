// Renders every page of the editor to PNG (set CM_SNAPSHOT_DIR to choose where; default: temp dir) and checks that the
// pages construct, lay out, repaint and tick without asserting. Look at the PNGs to review visual quality.
#include "TestHarness.h"
#include "PluginEditor.h"
#include "UI/Ticker.h"
#include <cstdlib>

using namespace crowdmike;
using namespace crowdmike::ui;

namespace {
juce::File snapshotDir() {
    if (const char* d = std::getenv("CM_SNAPSHOT_DIR")) { juce::File f(d); f.createDirectory(); return f; }
    { auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cm_snapshots"); d.createDirectory(); return d; }
}
bool savePng(const juce::Image& image, const juce::File& file) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream out(file);
    return out.openedOk() && juce::PNGImageFormat().writeImageToStream(image, out);
}
}

CM_TEST(ui_all_pages_render_without_error) {
    juce::ScopedJuceInitialiser_GUI gui;
    CrowdMikeAudioProcessor p;
    p.prepareToPlay(48000.0, 256);
    p.applyWiringTemplate(3);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* editor = dynamic_cast<CrowdMikeAudioProcessorEditor*>(base.get());
    CM_EXPECT(editor != nullptr);
    if (editor == nullptr) return;
    editor->setSize(1600, 900);
    // push some audio so meters have content
    juce::AudioBuffer<float> b(2, 256);
    juce::MidiBuffer midi;
    for (int blk = 0; blk < 40; ++blk) {
        for (int s = 0; s < 256; ++s) { const float v = 0.3f * std::sin(0.05f * static_cast<float>(blk * 256 + s)); b.setSample(0, s, v); b.setSample(1, s, -v); }
        p.processBlock(b, midi);
    }
    const auto dir = snapshotDir();
    for (int i = 0; i < static_cast<int>(PageId::Count); ++i) {
        editor->showPage(static_cast<PageId>(i));
        Tickable::tickAllForTesting();
        auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        CM_EXPECT(image.isValid());
        CM_EXPECT(savePng(image, dir.getChildFile(juce::String(pageName(static_cast<PageId>(i))) + ".png")));
    }
    editor->getContext().select({ Selection::Kind::Input, 0 });
    editor->showPage(PageId::Inputs);
    CM_EXPECT(savePng(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f), dir.getChildFile("Inputs_selected.png")));
}
