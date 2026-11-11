// Audit finding W3: preset save/load dialogs used to be a process-global FileChooser whose callbacks captured the
// processor by reference and outlived the editor. They are now owned by the EditorContext and guarded by the anchor.
#include "TestHarness.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/AsyncFileChooser.h"
#include "UI/EditKit.h"

using namespace crowdmike;

CM_TEST(preset_dialog_guarded_callback_runs_only_while_the_anchor_exists) {
    juce::ScopedJuceInitialiser_GUI gui;
    int calls = 0;
    auto anchor = std::make_unique<juce::Component>();
    juce::FileChooser dummy("test", juce::File(), "*", false);

    auto alive = ui::AsyncFileChooser::guarded(anchor.get(), [&](const juce::FileChooser&) { ++calls; });
    alive(dummy);
    CM_EXPECT(calls == 1);

    auto guarded = ui::AsyncFileChooser::guarded(anchor.get(), [&](const juce::FileChooser&) { ++calls; });
    anchor.reset();                      // the editor (and its button) is gone before the dialog returns
    guarded(dummy);
    CM_EXPECT(calls == 1);

    auto empty = ui::AsyncFileChooser::guarded(nullptr, {});
    empty(dummy);                        // a missing callback is tolerated
    CM_EXPECT(calls == 1);
}

CM_TEST(preset_dialog_owner_destruction_cancels_a_pending_dialog) {
    juce::ScopedJuceInitialiser_GUI gui;
    int calls = 0;
    juce::Component parent, anchor;   // the JUCE dialog is kept inside `parent` (no desktop window in headless CI)
    parent.setSize(1280, 720);
    parent.addAndMakeVisible(anchor);
    {
        ui::AsyncFileChooser dialogs;
        const int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        const auto start = juce::File::getSpecialLocation(juce::File::tempDirectory);
        dialogs.launch("Load preset", start, "*.cmpreset", flags, &anchor, [&](const juce::FileChooser&) { ++calls; }, false, &parent);
        CM_EXPECT(parent.getNumChildComponents() == 2);   // anchor + the dialog
        // Launching again replaces the pending dialog instead of running two at once.
        dialogs.launch("Save preset", start, "*.cmpreset", flags, &anchor, [&](const juce::FileChooser&) { ++calls; }, false, &parent);
        CM_EXPECT(parent.getNumChildComponents() == 2);
        dialogs.cancel();
        CM_EXPECT(parent.getNumChildComponents() == 1);   // cancelled: the dialog is gone
        dialogs.launch("Load preset", start, "*.cmpreset", flags, &anchor, [&](const juce::FileChooser&) { ++calls; }, false, &parent);
        CM_EXPECT(parent.getNumChildComponents() == 2);
    }   // owner destroyed with the dialog open: the chooser drops its callback and closes the dialog
    CM_EXPECT(parent.getNumChildComponents() == 1);
    juce::Timer::callPendingTimersSynchronously();
    CM_EXPECT(calls == 0);
}

CM_TEST(preset_dialog_lives_in_the_editor_context_and_dies_with_the_editor) {
    juce::ScopedJuceInitialiser_GUI gui;
    auto processor = std::make_unique<CrowdMikeAudioProcessor>();
    {
        auto editor = std::make_unique<CrowdMikeAudioProcessorEditor>(*processor);
        auto& dialogs = editor->getContext().dialogs;
        juce::Component anchor;
        editor->addAndMakeVisible(anchor);
        const int before = editor->getNumChildComponents();
        dialogs.launch("Load preset", juce::File::getSpecialLocation(juce::File::tempDirectory), "*.cmpreset",
                       juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, &anchor,
                       [](const juce::FileChooser&) { CM_EXPECT(false); }, false, editor.get());
        CM_EXPECT(editor->getNumChildComponents() == before + 1);   // the dialog lives inside the editor
        editor.reset();                  // host closes the editor while the dialog is pending
    }
    juce::Timer::callPendingTimersSynchronously();
    processor.reset();                   // then destroys the plug-in: nothing is left that can call back into it
    CM_EXPECT(true);
}
