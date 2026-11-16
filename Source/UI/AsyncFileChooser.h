#pragma once
// Editor-owned asynchronous file chooser. The chooser lives exactly as long as its owner (the EditorContext, so the
// editor): destroying the owner cancels a pending dialog, because juce::FileChooser drops its callback on
// destruction, and every callback is additionally guarded by a component that must still exist. A dialog accepted
// after the editor closed can therefore never reach the processor through a dangling reference.
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace crowdmike::ui {
class AsyncFileChooser {
public:
    using Callback = std::function<void(const juce::FileChooser&)>;

    AsyncFileChooser() = default;
    ~AsyncFileChooser() { cancel(); }
    AsyncFileChooser(const AsyncFileChooser&) = delete;
    AsyncFileChooser& operator=(const AsyncFileChooser&) = delete;

    /** Wraps `fn` so that it runs only while `guard` still exists (pure helper, unit-tested). */
    static Callback guarded(juce::Component::SafePointer<juce::Component> guard, Callback fn) {
        return [guard, fn = std::move(fn)](const juce::FileChooser& fc) {
            if (guard != nullptr && fn) fn(fc);
        };
    }

    /** Opens a dialog; a dialog still pending from an earlier launch is cancelled first. `guard` is the component
        (normally the button that opened the menu) whose lifetime bounds the callback. `useNative` false forces the
        JUCE dialog and `parent` keeps that dialog inside a component instead of on the desktop (tests, headless
        runs); production callers leave both at their defaults. */
    void launch(const juce::String& title, const juce::File& startingFile, const juce::String& filters, int flags,
                juce::Component* guard, Callback fn, bool useNative = true, juce::Component* parent = nullptr) {
        cancel();
        chooser = std::make_unique<juce::FileChooser>(title, startingFile, filters, useNative, false, parent);
        chooser->launchAsync(flags, guarded(juce::Component::SafePointer<juce::Component>(guard), std::move(fn)));
    }

    /** Closes a pending dialog without invoking its callback. */
    void cancel() noexcept { chooser.reset(); }

private:
    std::unique_ptr<juce::FileChooser> chooser;
};
}
