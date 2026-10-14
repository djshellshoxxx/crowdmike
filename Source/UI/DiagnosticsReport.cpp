#include "DiagnosticsReport.h"

namespace crowdmike::ui {
LatencyBreakdown latencyBreakdown(CrowdMikeAudioProcessor& p) {
    auto& m = p.getMeters();
    LatencyBreakdown l;
    l.sampleRate = m.sampleRate.load(std::memory_order_relaxed);
    l.algorithmic = m.latencySamples.load(std::memory_order_relaxed);
    l.block = m.blockSize.load(std::memory_order_relaxed);
    if (auto* hs = p.getHostServices()) {
        const auto st = hs->getDeviceStatus();
        l.deviceIn = st.inputLatencySamples;
        l.deviceOut = st.outputLatencySamples;
        if (l.sampleRate <= 0.0) l.sampleRate = st.sampleRate;
    }
    return l;
}

juce::String samplesToMs(int samples, double sr) {
    return juce::String(samples) + " smp" + (sr > 0.0 ? " (" + juce::String(samples * 1000.0 / sr, 2) + " ms)" : juce::String());
}

juce::StringArray diagnosticsLines(CrowdMikeAudioProcessor& p) {
    auto& m = p.getMeters();
    juce::StringArray s;
    const auto lat = latencyBreakdown(p);
    s.add("== Engine");
    s.add("CrowdMike " + juce::String(CrowdMikeAudioProcessor::kAppVersion) + "  " + juce::SystemStats::getOperatingSystemName());
    s.add("Sample rate " + juce::String(lat.sampleRate / 1000.0, 1) + " kHz, block " + juce::String(lat.block)
          + ", I/O " + juce::String(p.getActiveInputChannelCount()) + " in / " + juce::String(p.getActiveOutputChannelCount()) + " out");
    s.add("CPU load " + juce::String(m.cpuLoad.load() * 100.0f, 1) + " % of block budget");
    s.add("Blocks " + juce::String(static_cast<juce::int64>(m.blocks.load())) + ", overruns " + juce::String(static_cast<juce::int64>(m.overruns.load())));
    if (auto* hs = p.getHostServices()) {
        const auto st = hs->getDeviceStatus();
        s.add("Device " + (st.deviceName.isNotEmpty() ? st.deviceName : juce::String("(none)")) + " [" + st.typeName + "] "
              + (st.deviceLost ? "LOST" : (st.deviceOpen ? "open" : "closed")) + ", xruns " + juce::String(st.xruns)
              + ", device CPU " + juce::String(st.cpuLoad * 100.0f, 1) + " %");
        if (st.error.isNotEmpty()) s.add("Device error: " + st.error);
    } else {
        s.add("Host-controlled I/O (plug-in): device xruns are reported by the host");
    }
    s.add("Mode: " + juce::String(m.panicActive.load() ? "PANIC " : "") + (m.bypassActive.load() ? "BYPASS " : "")
          + (m.rehearsalActive.load() ? "REHEARSAL " : "") + (m.soundcheckActive.load() ? "SOUNDCHECK " : "") + "live");
    s.add("== Route latency (input -> output)");
    s.add("Algorithmic (plug-in report) " + samplesToMs(lat.algorithmic, lat.sampleRate));
    s.add("Block / buffer " + samplesToMs(lat.block, lat.sampleRate));
    s.add("Device input " + samplesToMs(lat.deviceIn, lat.sampleRate) + ", output " + samplesToMs(lat.deviceOut, lat.sampleRate));
    s.add("Total estimate " + samplesToMs(lat.total(), lat.sampleRate));
    const auto rs = p.getRecorder().getStatus();
    s.add("== Recorder");
    s.add(juce::String(rs.recording ? "RECORDING" : "idle") + ", take " + juce::String(rs.take) + ", elapsed " + juce::String(rs.elapsedSeconds, 1) + " s");
    s.add("Dropouts " + juce::String(rs.dropouts) + ", errors " + juce::String(rs.errors) + ", FIFO fill " + juce::String(juce::roundToInt(rs.fifoFill * 100.0)) + " %"
          + ", free disk " + (rs.freeDiskGB >= 0.0 ? juce::String(rs.freeDiskGB, 1) + " GB" : juce::String("unknown")));
    if (rs.lastError.isNotEmpty()) s.add("Last error: " + rs.lastError);
    auto& fb = p.getEngine().feedback();
    s.add("== Analysis");
    s.add("Feedback worker " + juce::String(p.getAnalysisThread().isActive() ? "running" : "stopped") + ", passes "
          + juce::String(static_cast<juce::int64>(p.getAnalysisThread().getRunCount())) + ", tap " + juce::String(p.getEngine().getFeedbackTap())
          + ", dropped " + juce::String(static_cast<juce::int64>(fb.getDroppedSamples())) + " smp");
    std::size_t histBytes = p.getEngine().masterHistory().getMemoryBytes();
    for (int i = 0; i < kMaxInputs; ++i) histBytes += p.getEngine().inputHistory(i).getMemoryBytes();
    for (int g = 0; g < kMaxGroups; ++g) histBytes += p.getEngine().groupHistory(g).getMemoryBytes();
    s.add("Level-history memory " + juce::String(static_cast<double>(histBytes) / 1024.0, 1) + " KiB");
    s.add("== Output limiters (gain reduction)");
    juce::String line;
    for (int o = 0; o < p.getActiveOutputChannelCount(); ++o) {
        line << "Out" << (o + 1) << " " << juce::String(std::abs(m.outLimGrDb[static_cast<size_t>(o)].load()), 1) << " dB"
             << (m.outClip[static_cast<size_t>(o)].load() ? " CLIP" : "") << "   ";
        if ((o + 1) % 4 == 0) { s.add(line.trimEnd()); line.clear(); }
    }
    if (line.isNotEmpty()) s.add(line.trimEnd());
    s.add("Master limiter GR " + juce::String(std::abs(m.masterLimGrDb.load()), 1) + " dB, master comp GR " + juce::String(std::abs(m.masterGrDb.load()), 1) + " dB");
    return s;
}

juce::String buildDiagnosticsReport(CrowdMikeAudioProcessor& p) {
    return "CrowdMike diagnostics " + juce::Time::getCurrentTime().toISO8601(true) + "\n" + diagnosticsLines(p).joinIntoString("\n") + "\n";
}
}
