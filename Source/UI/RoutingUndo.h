#pragma once
// Undo/redo for routing edits (GUI_SPEC section 12). RoutingState is not a parameter tree, so routing edits are wrapped
// in juce::UndoableActions on the processor's UndoManager. All writes go through the lock-free RoutingState setters;
// the engine ramps the changes (ROUTING_SPEC section 7).
#include <juce_data_structures/juce_data_structures.h>
#include "Routing/RoutingState.h"

namespace crowdmike::ui {
enum class RouteMatrix { Direct, Assign, Bus };

inline float getRoute(const RoutingState& s, RouteMatrix m, int a, int b) noexcept {
    switch (m) {
        case RouteMatrix::Direct: return s.getDirect(a, b);   // a = input, b = output
        case RouteMatrix::Assign: return s.getAssign(a, b);   // a = input, b = group
        case RouteMatrix::Bus: return s.getBus(a, b);         // a = bus row, b = output
    }
    return 0.0f;
}
inline bool setRoute(RoutingState& s, RouteMatrix m, int a, int b, float v) noexcept {
    switch (m) {
        case RouteMatrix::Direct: return s.setDirect(a, b, v);
        case RouteMatrix::Assign: return s.setAssign(a, b, v);
        case RouteMatrix::Bus: return s.setBus(a, b, v);
    }
    return false;
}

// One matrix cell old -> new. Consecutive edits of the same cell inside one transaction coalesce.
class RoutingCellAction final : public juce::UndoableAction {
public:
    RoutingCellAction(RoutingState& s, RouteMatrix m, int a, int b, float oldValue, float newValue)
        : state(s), matrix(m), first(a), second(b), oldV(oldValue), newV(newValue) {}
    bool perform() override { return setRoute(state, matrix, first, second, newV); }
    bool undo() override { return setRoute(state, matrix, first, second, oldV); }
    int getSizeInUnits() override { return 1; }
    juce::UndoableAction* createCoalescedAction(juce::UndoableAction* next) override {
        if (auto* n = dynamic_cast<RoutingCellAction*>(next))
            if (&n->state == &state && n->matrix == matrix && n->first == first && n->second == second)
                return new RoutingCellAction(state, matrix, first, second, oldV, n->newV);
        return nullptr;
    }
private:
    RoutingState& state;
    RouteMatrix matrix;
    int first, second;
    float oldV, newV;
};

// Whole-routing snapshot (wiring templates, bulk edits).
class RoutingSnapshotAction final : public juce::UndoableAction {
public:
    RoutingSnapshotAction(RoutingState& s, juce::ValueTree beforeTree, juce::ValueTree afterTree)
        : state(s), before(std::move(beforeTree)), after(std::move(afterTree)) {}
    bool perform() override { return state.fromValueTree(after); }
    bool undo() override { return state.fromValueTree(before); }
    int getSizeInUnits() override { return 32; }
private:
    RoutingState& state;
    juce::ValueTree before, after;
};

// Records old -> new for one cell as its own undo transaction and applies `newValue`.
inline void performRouteEdit(juce::UndoManager& um, RoutingState& s, RouteMatrix m, int a, int b, float oldValue, float newValue,
                             const juce::String& name = "Routing change") {
    um.beginNewTransaction(name);
    um.perform(new RoutingCellAction(s, m, a, b, oldValue, newValue), name);
}
}
