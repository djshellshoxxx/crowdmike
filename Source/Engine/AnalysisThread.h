#pragma once
// AnalysisThread -- low-priority worker that runs FeedbackDetector::analyse() (and any other registered analysis job)
// every ~25 ms while at least one view needs it (ENGINE_SPEC section 11 "optional analysis worker").
//  * acquire()/release() are reference counted (message thread); the thread starts on the first acquire and is stopped
//    (joined) on the last release, so nothing runs while the Diagnostics page is hidden.
//  * The audio thread is never touched: the detector's FIFO is the only hand-over (FeedbackDetector contract).
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

namespace crowdmike {
class AnalysisThread final : private juce::Thread {
public:
    explicit AnalysisThread(std::function<void()> job, int periodMs = 25)
        : juce::Thread("CrowdMike analysis"), work(std::move(job)), period(periodMs) {}
    ~AnalysisThread() override { stopThread(2000); }

    void acquire();
    void release();
    bool isActive() const noexcept { return users > 0; }
    std::uint64_t getRunCount() const noexcept { return runs.load(std::memory_order_relaxed); }
    // Runs the job once synchronously (tests / when the thread is not wanted).
    void runOnceForTesting() { if (work) work(); runs.fetch_add(1, std::memory_order_relaxed); }

private:
    void run() override;
    std::function<void()> work;
    int period;
    int users = 0;
    std::atomic<std::uint64_t> runs { 0 };
};
}
