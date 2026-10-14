#include "FileNaming.h"

namespace crowdmike::rec {

namespace {
juce::String two(int v) { return juce::String(v).paddedLeft('0', 2); }

juce::String capUtf8(const juce::String& s, int maxBytes) {
    if ((int) s.getNumBytesAsUTF8() <= maxBytes) return s;
    juce::String out;
    int bytes = 0;
    for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p) {
        const auto c = *p;
        const int n = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (bytes + n > maxBytes) break;
        bytes += n;
        out += juce::String::charToString(c);
    }
    return out;
}

juce::String trimEnd(juce::String s) {
    while (s.isNotEmpty() && (s.getLastCharacter() == '.' || s.getLastCharacter() == ' ')) s = s.dropLastCharacters(1);
    return s;
}
} // namespace

bool FileNaming::isWindowsReservedName(const juce::String& name) {
    auto stem = name.upToFirstOccurrenceOf(".", false, false).trimEnd().toUpperCase();
    static const char* fixed[] = { "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$" };
    for (auto* f : fixed)
        if (stem == f) return true;
    if (stem.length() == 4 && (stem.startsWith("COM") || stem.startsWith("LPT"))) {
        const auto d = stem.getLastCharacter();
        // COM1-9 / LPT1-9 and the superscript digits Windows also treats as reserved.
        if ((d >= '1' && d <= '9') || d == 0xB9 || d == 0xB2 || d == 0xB3) return true;
    }
    return false;
}

juce::String FileNaming::sanitise(const juce::String& name, int maxUtf8Bytes) {
    juce::String out;
    out.preallocateBytes(name.getNumBytesAsUTF8() + 4);
    for (auto p = name.getCharPointer(); ! p.isEmpty(); ++p) {
        const auto c = *p;
        const bool bad = c < 32 || c == 127 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\'
                         || c == '|' || c == '?' || c == '*' || (c >= 0x80 && c < 0xA0) /* C1 controls */
                         || c == 0xFEFF || c == 0x200B /* invisible */;
        out += bad ? juce::String("_") : juce::String::charToString(c);
    }
    out = out.trimStart();
    if (out.startsWithChar('.')) out = "_" + out.substring(1);
    out = trimEnd(out);
    if (isWindowsReservedName(out)) out = "_" + out;
    out = trimEnd(capUtf8(out, juce::jmax(8, maxUtf8Bytes)));
    if (out.isEmpty()) out = "untitled";
    return out;
}

juce::String FileNaming::expandPattern(const juce::String& pattern, const NameTokens& t, int maxUtf8Bytes) {
    const auto& tm = t.time;
    const auto date = juce::String(tm.getYear()) + "-" + two(tm.getMonth() + 1) + "-" + two(tm.getDayOfMonth());
    const auto time = two(tm.getHours()) + "-" + two(tm.getMinutes()) + "-" + two(tm.getSeconds());
    // Token values must not inject path separators; sanitise each (but allow empty).
    auto val = [](const juce::String& v) { return v.trim().isEmpty() ? juce::String() : sanitise(v, 64); };

    auto s = pattern;
    s = s.replace("{date}", date).replace("{time}", time).replace("{scene}", val(t.scene))
         .replace("{source}", val(t.source)).replace("{show}", val(t.show)).replace("{venue}", val(t.venue))
         .replace("{take}", two(juce::jmax(0, t.take)));
    // Collapse separators left behind by empty tokens.
    for (auto* dup : { "__", "--", "_-_", "  " })
        while (s.contains(dup)) s = s.replace(dup, juce::String(dup).substring(0, 1));
    s = s.trimCharactersAtStart("_- ").trimCharactersAtEnd("_- ");
    return sanitise(s, maxUtf8Bytes);
}

juce::File FileNaming::uniqueFile(const juce::File& folder, const juce::String& base, const juce::String& ext,
                                  const juce::StringArray& alsoTaken) {
    for (int i = 1; i < 100000; ++i) {
        const auto name = i == 1 ? base + ext : base + "_" + juce::String(i) + ext;
        const auto f = folder.getChildFile(name);
        if (! f.exists() && ! alsoTaken.contains(f.getFullPathName())) return f;
    }
    return folder.getChildFile(base + "_" + juce::Uuid().toString() + ext);
}

} // namespace crowdmike::rec
