#include "MarkerExport.h"
#include <cmath>

namespace crowdmike::rec::MarkerExport {

namespace {
juce::String csvField(const juce::String& s) {
    if (! s.containsAnyOf(",\"\r\n")) return s;
    return "\"" + s.replace("\"", "\"\"") + "\"";
}
juce::String oneLine(const juce::String& s) { return s.replaceCharacters("\t\r\n", "   "); }
juce::String labelOf(const Marker& m) { return m.label.isNotEmpty() ? m.label : markerTypeToString(m.type); }
} // namespace

juce::String timecode(double seconds) {
    if (! std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const auto totalMs = (juce::int64) std::llround(seconds * 1000.0);
    const auto ms = (int) (totalMs % 1000), s = (int) ((totalMs / 1000) % 60), m = (int) ((totalMs / 60000) % 60);
    const auto h = (int) (totalMs / 3600000);
    return juce::String(h) + ":" + juce::String(m).paddedLeft('0', 2) + ":" + juce::String(s).paddedLeft('0', 2) + "."
           + juce::String(ms).paddedLeft('0', 3);
}

juce::String toCsv(const SessionInfo& info) {
    juce::String t = "index,seconds,timecode,frame,type,label,scene,wallClock\n";
    int i = 1;
    for (auto& m : info.markers)
        t << i++ << "," << juce::String(m.seconds, 6) << "," << timecode(m.seconds) << "," << m.frame << ","
          << markerTypeToString(m.type) << "," << csvField(m.label) << "," << csvField(m.scene) << ","
          << m.wallClock.toISO8601(true) << "\n";
    return t;
}

juce::String toAudacityLabels(const SessionInfo& info) {
    juce::String t;
    for (auto& m : info.markers) {
        const auto s = juce::String(m.seconds, 6);
        t << s << "\t" << s << "\t" << oneLine(labelOf(m)) << "\n";
    }
    return t;
}

juce::String toReaperCsv(const SessionInfo& info) {
    juce::String t = "#,Name,Start,End,Length,Color\n";
    int i = 1;
    for (auto& m : info.markers) t << "M" << i++ << "," << csvField(labelOf(m)) << "," << timecode(m.seconds) << ",,,\n";
    return t;
}

bool writeAll(const juce::File& sessionJson, const SessionInfo& info, juce::String& error) {
    const auto stem = sessionJson.getFileNameWithoutExtension();
    const auto dir = sessionJson.getParentDirectory();
    const auto csv = dir.getChildFile(stem + "_markers.csv"), txt = dir.getChildFile(stem + "_markers.txt");
    if (! csv.replaceWithText(toCsv(info), false, false, "\n") || ! txt.replaceWithText(toAudacityLabels(info), false, false, "\n")) {
        error = "Could not write marker export next to " + sessionJson.getFullPathName();
        return false;
    }
    return true;
}

} // namespace crowdmike::rec::MarkerExport
