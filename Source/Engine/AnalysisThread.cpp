#include "AnalysisThread.h"

namespace crowdmike {
AnalysisThread::ScopedSuspension::ScopedSuspension(AnalysisThread& w) : worker(w), lock(w.lifecycleMutex)
{
    worker.stopThread(-1);  // prepare must wait for all current reads to finish before replacing their buffers
}

AnalysisThread::ScopedSuspension::~ScopedSuspension()
{
    if (worker.users.load(std::memory_order_relaxed) > 0)
        worker.startThread(juce::Thread::Priority::low);
}

void AnalysisThread::acquire()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex);
    if (users++ == 0)
        startThread(juce::Thread::Priority::low);
}

void AnalysisThread::release()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex);
    if (users <= 0) return;
    if (--users == 0)
        stopThread(1000);
}

void AnalysisThread::run()
{
    while (!threadShouldExit()) {
        const auto start = juce::Time::getMillisecondCounter();
        if (work) work();
        runs.fetch_add(1, std::memory_order_relaxed);
        const int spent = static_cast<int>(juce::Time::getMillisecondCounter() - start);
        wait(juce::jmax(2, period - spent));
    }
}
}
