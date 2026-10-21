#pragma once
// Apply / undo adapter for Line Check proposals (docs/LINE_CHECK_SPEC.md section 4). Pure glue: it changes parameters through
// caller-supplied functions inside ONE UndoManager transaction, so the existing undo system reverts a whole apply with one undo().
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <vector>
#include "Engine/LineCheck.h"

namespace crowdmike::linecheck {
struct ApplyTarget {
    juce::UndoManager& undo;
    std::function<void(const juce::String& paramId, float actualValue)> setParam;
    std::function<float(const juce::String& paramId)> getParam;     // current actual value (used for re-checks and polarity flips)
};

struct ApplyOptions {
    bool trim = true, hpf = true, polarity = false;
    bool newTransaction = true;                                      // false: join the caller's open transaction
};

// Applies the results of the listed inputs. Returns the number of parameter writes made.
int applyProposals(ApplyTarget&, const std::vector<Result>& results, const std::vector<int>& inputs, const ApplyOptions& options = {});
// Quick start: HPF 100 Hz on for every active input whose HPF is off (no new transaction: call after applyWiringTemplate).
int applyQuickStartDefaults(ApplyTarget&, int activeInputs);
}
