#include "AnalysisThread.h"

namespace crowdmike {
void AnalysisThread::acquire()
{
    if (users++ == 0)
        startThread(juce::Thread::Priority::low);
}

void AnalysisThread::release()
{
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
