#pragma once
// Marker export (RECORDING_SPEC 8.2): turns the marker list of a session into files other tools understand.
// Pure text formatting + one file writer; never used on the audio thread.
#include "SessionMetadata.h"

namespace crowdmike::rec::MarkerExport {

/** index,seconds,timecode,frame,type,label,scene,wallClock  (RFC 4180 quoting, UTF-8, header row). */
juce::String toCsv(const SessionInfo&);
/** Audacity label track: "seconds<TAB>seconds<TAB>label" per point marker. */
juce::String toAudacityLabels(const SessionInfo&);
/** REAPER marker/region manager import: "#,Name,Start,End,Length,Color" with H:MM:SS.mmm start times. */
juce::String toReaperCsv(const SessionInfo&);

/** "H:MM:SS.mmm" */
juce::String timecode(double seconds);

/** Writes <session stem>_markers.csv and <session stem>_markers.txt (Audacity labels) next to the session JSON. */
bool writeAll(const juce::File& sessionJson, const SessionInfo&, juce::String& error);

} // namespace crowdmike::rec::MarkerExport
