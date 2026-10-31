#include "TestHarness.h"
#include "Engine/AnalysisThread.h"
#include "PluginProcessor.h"
#include <atomic>
#include <thread>

CM_TEST(analysis_suspension_joins_current_job_and_blocks_restart_until_resume) {
    juce::WaitableEvent jobStarted, finishJob(true), suspensionEntered, leaveSuspension;
    juce::WaitableEvent acquireStarted, acquireFinished;
    std::atomic<int> jobs { 0 };
    crowdmike::AnalysisThread worker([&] {
        jobs.fetch_add(1);
        jobStarted.signal();
        finishJob.wait(2000);
    }, 5);
    worker.acquire();
    CM_EXPECT(jobStarted.wait(2000));
    std::thread suspender([&] {
        crowdmike::AnalysisThread::ScopedSuspension suspended(worker);
        suspensionEntered.signal();
        leaveSuspension.wait(2000);
    });
    CM_EXPECT(!suspensionEntered.wait(30));  // in-flight job still owns its state
    finishJob.signal();
    CM_EXPECT(suspensionEntered.wait(2000));
    const int jobsBefore = jobs.load();
    std::thread acquirer([&] {
        acquireStarted.signal();
        worker.acquire();
        acquireFinished.signal();
    });
    CM_EXPECT(acquireStarted.wait(2000));
    CM_EXPECT(!acquireFinished.wait(30));
    CM_EXPECT(jobs.load() == jobsBefore);
    leaveSuspension.signal();
    suspender.join();
    CM_EXPECT(acquireFinished.wait(2000));
    acquirer.join();
    CM_EXPECT(jobStarted.wait(2000));
    CM_EXPECT(worker.isActive());
    worker.release();
    CM_EXPECT(worker.isActive());       // the acquire blocked by suspension is retained
    worker.release();
    CM_EXPECT(!worker.isActive());
}

CM_TEST(analysis_suspension_does_not_start_a_worker_without_users) {
    crowdmike::AnalysisThread worker([] {}, 2);
    { crowdmike::AnalysisThread::ScopedSuspension suspended(worker); }
    CM_EXPECT(!worker.isActive());
    CM_EXPECT(worker.getRunCount() == 0);
}

CM_TEST(analysis_processor_reprepare_preserves_existing_feedback_users) {
    CrowdMikeAudioProcessor processor;
    processor.prepareToPlay(48000.0, 128);
    auto& worker = processor.getAnalysisThread();
    worker.acquire();
    worker.acquire();
    for (double rate : { 44100.0, 96000.0, 48000.0 }) {
        processor.prepareToPlay(rate, 64);
        CM_EXPECT(worker.isActive());
        CM_EXPECT_NEAR(processor.getEngine().getSampleRate(), rate, 1.0e-6);
    }
    const auto before = worker.getRunCount();
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (worker.getRunCount() <= before && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(2);
    CM_EXPECT(worker.getRunCount() > before);
    worker.release();
    CM_EXPECT(worker.isActive());
    worker.release();
    CM_EXPECT(!worker.isActive());
}
