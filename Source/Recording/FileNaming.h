#pragma once
// File-name helpers for the recorder (RECORDING_SPEC section 5). Pure functions, any thread except the audio thread.
#include <juce_core/juce_core.h>

namespace crowdmike::rec {

struct NameTokens {
    juce::Time time;               // {date} = YYYY-MM-DD, {time} = HH-MM-SS (local time)
    juce::String scene, source, show, venue;
    int take = 1;                  // {take} = zero-padded to 2 digits ("07")
};

class FileNaming {
public:
    /** Expands {date} {time} {scene} {source} {show} {venue} {take} (unknown tokens are left verbatim, then
        sanitised). Empty token values do not leave doubled/leading/trailing separators. The result is sanitised. */
    static juce::String expandPattern(const juce::String& pattern, const NameTokens& tokens, int maxUtf8Bytes = 150);

    /** Makes a single path component safe on Windows, macOS and Linux:
        - removes control characters and  < > : " / \ | ? *  (replaced by '_')
        - strips trailing dots/spaces and leading spaces; a leading '.' becomes '_' (no hidden files)
        - prefixes Windows reserved device names (CON, PRN, AUX, NUL, COM1-9, LPT1-9, also with an extension) with '_'
        - keeps Unicode, caps the UTF-8 length at maxUtf8Bytes without splitting a code point
        - never returns an empty string ("untitled"). */
    static juce::String sanitise(const juce::String& name, int maxUtf8Bytes = 150);

    /** Returns folder/base+ext, or folder/base_2+ext, base_3 ... - the first name that neither exists on disk
        nor is contained in `alsoTaken` (full paths). */
    static juce::File uniqueFile(const juce::File& folder, const juce::String& base, const juce::String& extension,
                                 const juce::StringArray& alsoTaken = {});

    static bool isWindowsReservedName(const juce::String& name);
};

} // namespace crowdmike::rec
