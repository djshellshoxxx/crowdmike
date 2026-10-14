#include "Ticker.h"

namespace crowdmike::ui {
namespace {
struct Hub final : private juce::Timer {
    std::vector<Tickable*> items;
    bool dispatching = false;
    std::vector<Tickable*> pendingRemove;
    void add(Tickable* t) { items.push_back(t); if (!isTimerRunning() && juce::MessageManager::getInstanceWithoutCreating() != nullptr) startTimerHz(30); }
    void remove(Tickable* t) {
        if (dispatching) { pendingRemove.push_back(t); return; }
        items.erase(std::remove(items.begin(), items.end(), t), items.end());
        if (items.empty()) stopTimer();
    }
    void dispatch() {
        dispatching = true;
        for (size_t i = 0; i < items.size(); ++i) {
            if (std::find(pendingRemove.begin(), pendingRemove.end(), items[i]) == pendingRemove.end()) items[i]->tick();
        }
        dispatching = false;
        for (auto* t : pendingRemove) items.erase(std::remove(items.begin(), items.end(), t), items.end());
        pendingRemove.clear();
        if (items.empty()) stopTimer();
    }
    void timerCallback() override { dispatch(); }
};
Hub& hub() { static Hub h; return h; }
}
Tickable::Tickable() { hub().add(this); }
Tickable::~Tickable() { hub().remove(this); }
void Tickable::tickAllForTesting() { hub().dispatch(); }
}
