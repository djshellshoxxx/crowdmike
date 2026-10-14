#pragma once
// Shared state between the editor shell and its pages: what is selected, navigation, transient messages.
#include <functional>
#include <vector>
#include "PluginProcessor.h"
#include "UI/Theme.h"

namespace crowdmike::ui {
enum class PageId { Live = 0, Inputs, Groups, Fx, Matrix, Record, Playback, Scenes, Settings, Diagnostics, Count };
const char* pageName(PageId) noexcept;

struct Selection {
    enum class Kind { None, Input, Group, Output, Fx, Master, Clip, Scene, Track } kind = Kind::None;
    int index = 0;
    bool operator==(const Selection& o) const noexcept { return kind == o.kind && index == o.index; }
};

class EditorContext {
public:
    explicit EditorContext(CrowdMikeAudioProcessor& p) : proc(p) {}

    struct Listener {
        virtual ~Listener() = default;
        virtual void selectionChanged() {}
        virtual void showModelChanged() {}          // names, macros, recorder settings, ui settings edited
    };

    CrowdMikeAudioProcessor& proc;
    std::function<void(PageId)> navigate;                       // set by the shell
    std::function<void(const juce::String&, bool isWarning)> notify;   // transient status-bar message

    const Selection& getSelection() const noexcept { return selection; }
    void select(Selection s) {
        if (s == selection) return;
        selection = s;
        for (auto* l : listeners) l->selectionChanged();
    }
    void showModelChanged() { proc.applyShowChanges(); for (auto* l : listeners) l->showModelChanged(); }
    void addListener(Listener* l) { listeners.push_back(l); }
    void removeListener(Listener* l) { listeners.erase(std::remove(listeners.begin(), listeners.end(), l), listeners.end()); }
    void say(const juce::String& m, bool warn = false) { if (notify) notify(m, warn); }

private:
    Selection selection;
    std::vector<Listener*> listeners;
};

// Base class of every page. Pages are created once and kept alive; pageShown() refreshes them.
class Page : public juce::Component, protected EditorContext::Listener {
public:
    explicit Page(EditorContext& c) : ctx(c), proc(c.proc) { ctx.addListener(this); setWantsKeyboardFocus(false); }
    ~Page() override { ctx.removeListener(this); }
    virtual void pageShown() {}
    virtual void pageHidden() {}
    void paint(juce::Graphics& g) override { g.fillAll(palette().background); }
protected:
    static const Palette& palette() noexcept;
    EditorContext& ctx;
    CrowdMikeAudioProcessor& proc;
};
}
