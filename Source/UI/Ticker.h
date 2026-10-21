#pragma once
// One shared 30 Hz GUI timer instead of one juce::Timer per meter. Message-thread only.
#include <juce_events/juce_events.h>
#include <algorithm>
#include <vector>

namespace crowdmike::ui {
class Tickable {
public:
    Tickable();
    virtual ~Tickable();
    virtual void tick() = 0;
    static void tickAllForTesting();     // lets tests drive meters without a message loop
private:
    Tickable(const Tickable&) = delete;
    Tickable& operator=(const Tickable&) = delete;
};
}
