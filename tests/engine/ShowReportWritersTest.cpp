// Show Report writers (SHOW_REPORT_SPEC section 9): golden files, structure, escaping, locale independence.
// Golden files live in tests/engine/golden/. Run with CM_UPDATE_GOLDEN=1 to regenerate them (then review the diff).
#include "TestHarness.h"
#include "Report/ReportWriters.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <locale>
#include <sstream>

using namespace crowdmike::report;

namespace {
// ------------------------------------------------------------------------------------------------ strict JSON parser (tests only)
struct J {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    double num = 0;
    bool b = false;
    std::string str;
    std::vector<J> arr;
    std::vector<std::pair<std::string, J>> obj;
    const J* get(const std::string& k) const {
        for (const auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

struct JsonParser {
    const std::string& s;
    size_t i = 0;
    bool ok = true;
    explicit JsonParser(const std::string& text) : s(text) {}
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    bool lit(const char* w) { const size_t n = std::char_traits<char>::length(w); if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }
    J value() {
        J v;
        ws();
        if (i >= s.size()) { ok = false; return v; }
        const char c = s[i];
        if (c == '{') {
            v.type = J::Obj; ++i; ws();
            if (i < s.size() && s[i] == '}') { ++i; return v; }
            while (ok) {
                ws();
                if (i >= s.size() || s[i] != '"') { ok = false; break; }
                std::string k = string();
                ws();
                if (i >= s.size() || s[i] != ':') { ok = false; break; }
                ++i;
                v.obj.emplace_back(k, value());
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                ok = false;
            }
        } else if (c == '[') {
            v.type = J::Arr; ++i; ws();
            if (i < s.size() && s[i] == ']') { ++i; return v; }
            while (ok) {
                v.arr.push_back(value());
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                ok = false;
            }
        } else if (c == '"') {
            v.type = J::Str; v.str = string();
        } else if (lit("true")) { v.type = J::Bool; v.b = true; }
        else if (lit("false")) { v.type = J::Bool; v.b = false; }
        else if (lit("null")) { v.type = J::Null; }
        else {
            const size_t st = i;
            if (i < s.size() && s[i] == '-') ++i;
            if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) { ok = false; return v; }
            if (s[i] == '0' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1]))) { ok = false; return v; }
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
            if (i < s.size() && s[i] == '.') { ++i; if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) { ok = false; return v; } while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i; }
            v.type = J::Num; v.num = std::strtod(s.substr(st, i - st).c_str(), nullptr);
        }
        return v;
    }
    std::string string() {
        std::string out;
        ++i;   // opening quote
        while (i < s.size() && s[i] != '"') {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x20) { ok = false; return out; }
            if (c == '\\') {
                ++i;
                if (i >= s.size()) { ok = false; return out; }
                switch (s[i]) {
                    case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
                    case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                    default: ok = false; return out;   // we never emit \u escapes
                }
                ++i;
                continue;
            }
            out += static_cast<char>(c);
            ++i;
        }
        if (i >= s.size()) { ok = false; return out; }
        ++i;   // closing quote
        return out;
    }
    static bool parse(const std::string& text, J& out) {
        JsonParser p(text);
        out = p.value();
        p.ws();
        return p.ok && p.i == text.size();
    }
};

bool validUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + static_cast<size_t>(n) > s.size()) return false;
        for (int k = 1; k < n; ++k) if ((static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0xC0) != 0x80) return false;
        i += static_cast<size_t>(n);
    }
    return true;
}

size_t count(const std::string& hay, const std::string& needle) {
    size_t n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size())) ++n;
    return n;
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

// ------------------------------------------------------------------------------------------------ fixture
ReportData fixture() {
    ReportData d;
    auto& m = d.meta;
    m.show = "Midnight <Tour> & Co";
    m.venue = "The \"Barn\"";
    m.scene = "Verse 1";
    m.appVersion = "0.2.0";
    m.sessionFile = "gig_2026-10-20_session.json";
    m.take = 2;
    m.startEpochMs = 1792555385000LL;   // 2026-10-21T04:03:05Z = 21:03:05 at UTC-7
    m.utcOffsetMinutes = -420;
    m.device = { "ALSA", "Test card", 48000.0, 256, 400, 8.33, 2, 2 };
    m.peakSensitivity = 0.5;
    m.peakProminenceDb = 7.5;
    m.peakMinSpacingSeconds = 28.5;
    m.autoMarkers = true;

    const float crowd[24] = { -52, -51.5f, -50, -47, -44, -41, -38.5f, -36, -34.2f, -33.1f, -35, -38, -41, -44, -46, -47, -45, -42, -39, -35, -30.4f, -27.8f, -31, -36 };
    for (int i = 0; i < 24; ++i) {
        Sample s;
        s.t = i;
        s.crowdDb = crowd[i];
        s.masterDb = crowd[i] + 3.5f;
        s.masterPeakDb = crowd[i] + 10.2f;
        s.lufs = crowd[i] + 2.1f;
        s.density = static_cast<float>(i) / 24.0f;
        s.recording = i >= 2 && i < 22;
        s.scene = i == 3 ? 1 : 0;
        s.fx = i == 5 ? 2 : (i == 12 ? 1 : 0);
        s.feedback = i == 9 ? 1 : 0;
        s.panic = i == 15 ? 1 : 0;
        s.xrun = i == 18 ? 1 : 0;
        s.clip = i == 20 ? 1 : 0;
        s.limiter = i == 21 ? 1 : 0;
        d.series.push_back(s);
    }
    d.stepSeconds = 1.0;
    d.peaks = { { 9.0, -33.1f, 12.4f, 11.0, true }, { 21.0, -27.8f, 9.9f, 23.0, false } };
    d.topMoments = { { 1, d.peaks[0] }, { 2, d.peaks[1] } };
    d.scenes = { { "Verse 1", 2, 3.0 }, { "Chorus <b>", 1, 13.0 } };
    d.sceneEvents = { { 3.0, "Verse 1" }, { 13.0, "Chorus <b>" }, { 20.0, "Verse 1" } };
    d.feedbackEvents = { { 9.0, 2500.4f, 0.83f } };
    d.markers = { { 6.5, "Big cheer & hat toss" }, { 17.0, "<img src=x onerror=alert(1)>" } };
    auto& s = d.summary;
    s.durationSeconds = 24.0; s.recordedSeconds = 20.0; s.peakCrowdAtSeconds = 21.0;
    s.averageCrowdDb = -40.2f; s.peakCrowdDb = -27.8f; s.maxLufs = -25.7f; s.meanLufs = -37.9f; s.maxMasterPeakDb = -17.6f;
    s.peakCount = 2; s.peaksSuppressed = 1; s.clipEvents = 1; s.limiterEvents = 1; s.feedbackEvents = 1; s.panicEvents = 1; s.xruns = 1; s.sceneRecalls = 3;
    s.fx = { 2, 1, 0 };
    return d;
}

std::string goldenPath(const char* name) {
    std::string here = __FILE__;
    const auto slash = here.find_last_of("/\\");
    return here.substr(0, slash + 1) + "golden/" + name;
}
std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
void expectGolden(const char* name, const std::string& actual) {
    const std::string path = goldenPath(name);
    if (std::getenv("CM_UPDATE_GOLDEN") != nullptr) {
        std::ofstream(path, std::ios::binary) << actual;
        return;
    }
    const std::string expected = readFile(path);
    if (expected.empty()) { std::cerr << "missing golden file " << path << " (run with CM_UPDATE_GOLDEN=1)\n"; ++cmtest::failures(); return; }
    if (expected != actual) {
        size_t k = 0;
        while (k < expected.size() && k < actual.size() && expected[k] == actual[k]) ++k;
        std::cerr << "golden mismatch in " << name << " at byte " << k << "\n  expected: ..." << expected.substr(k > 40 ? k - 40 : 0, 120)
                  << "\n  actual:   ..." << actual.substr(k > 40 ? k - 40 : 0, 120) << "\n";
        ++cmtest::failures();
    }
}
}

CM_TEST(report_writers_match_the_golden_files) {
    const auto d = fixture();
    expectGolden("show_report.html", renderHtml(d));
    expectGolden("show_report.csv", renderCsv(d));
    expectGolden("show_report.json", renderJson(d));
}

CM_TEST(report_writers_are_deterministic) {
    const auto d = fixture();
    CM_EXPECT(renderHtml(d) == renderHtml(d));
    CM_EXPECT(renderCsv(d) == renderCsv(d));
    CM_EXPECT(renderJson(d) == renderJson(d));
}

CM_TEST(report_csv_has_a_header_and_one_row_per_sample) {
    const auto d = fixture();
    const auto csv = renderCsv(d);
    std::vector<std::string> lines;
    std::istringstream is(csv);
    for (std::string l; std::getline(is, l);) lines.push_back(l);
    CM_EXPECT(lines.size() == d.series.size() + 1);
    CM_EXPECT(lines[0] == "time_s,crowd_db,density,master_db,master_peak_db,lufs_s,clip,limiter,feedback,panic,fx,scene,xrun,recording");
    CM_EXPECT(csv.back() == '\n' && csv.find('\r') == std::string::npos);
    for (size_t i = 1; i < lines.size(); ++i) CM_EXPECT(count(lines[i], ",") == 13);
    CM_EXPECT(lines[1] == "0.0,-52.00,0.000,-48.50,-41.80,-49.90,0,0,0,0,0,0,0,0");
    CM_EXPECT(lines[4] == "3.0,-47.00,0.125,-43.50,-36.80,-44.90,0,0,0,0,0,1,0,1");
    CM_EXPECT(lines[24] == "23.0,-36.00,0.958,-32.50,-25.80,-33.90,0,0,0,0,0,0,0,0");
    CM_EXPECT(renderCsv(ReportData {}) == std::string(kCsvHeader) + "\n");
}

CM_TEST(report_json_is_valid_and_follows_the_documented_schema) {
    const auto d = fixture();
    const auto text = renderJson(d);
    CM_EXPECT(validUtf8(text));
    J root;
    CM_EXPECT(JsonParser::parse(text, root));
    CM_EXPECT(root.type == J::Obj);
    auto num = [&](const J* n) { return n != nullptr && n->type == J::Num ? n->num : -9999.0; };
    CM_EXPECT(root.get("schema") && root.get("schema")->str == "crowdmike.show-report");
    CM_EXPECT(num(root.get("schemaVersion")) == 1.0);
    for (const char* k : { "generator", "show", "session", "device", "settings", "summary", "topMoments", "peaks", "scenes", "feedbackEvents", "markers", "series" })
        CM_EXPECT(root.get(k) != nullptr);
    CM_EXPECT(root.get("generator")->get("version")->str == "0.2.0");
    const J& show = *root.get("show");
    CM_EXPECT(show.get("name")->str == "Midnight <Tour> & Co");
    CM_EXPECT(show.get("venue")->str == "The \"Barn\"");
    CM_EXPECT(show.get("start")->str == "2026-10-20T21:03:05-07:00");
    CM_EXPECT(show.get("end")->str == "2026-10-20T21:03:29-07:00");
    CM_EXPECT(num(show.get("durationSeconds")) == 24.0);
    CM_EXPECT(root.get("session")->get("file")->str == "gig_2026-10-20_session.json" && num(root.get("session")->get("take")) == 2.0);
    CM_EXPECT(num(root.get("device")->get("sampleRate")) == 48000.0 && num(root.get("device")->get("latencySamples")) == 400.0);
    const J& sum = *root.get("summary");
    CM_EXPECT(num(sum.get("peakCount")) == 2.0 && num(sum.get("peaksSuppressed")) == 1.0 && num(sum.get("averageCrowdDb")) == -40.2);
    CM_EXPECT(num(sum.get("fx")->get("total")) == 3.0 && num(sum.get("fx")->get("echoThrows")) == 2.0);
    CM_EXPECT(root.get("topMoments")->arr.size() == 2);
    CM_EXPECT(root.get("topMoments")->arr[0].get("timecode")->str == "0:00:09");
    CM_EXPECT(root.get("topMoments")->arr[0].get("clock")->str == "21:03:14");
    CM_EXPECT(root.get("peaks")->arr[0].get("marker")->b && !root.get("peaks")->arr[1].get("marker")->b);
    CM_EXPECT(root.get("markers")->arr[1].get("label")->str == "<img src=x onerror=alert(1)>");
    const J& series = *root.get("series");
    CM_EXPECT(num(series.get("stepSeconds")) == 1.0);
    CM_EXPECT(series.get("columns")->arr.size() == 14 && series.get("columns")->arr[1].str == "crowdDb");
    CM_EXPECT(series.get("rows")->arr.size() == 24);
    for (const auto& row : series.get("rows")->arr) CM_EXPECT(row.type == J::Arr && row.arr.size() == 14);
    CM_EXPECT(series.get("rows")->arr[9].arr[1].num == -33.1);
    CM_EXPECT(series.get("rows")->arr[2].arr[13].num == 1.0);
}

CM_TEST(report_html_is_self_contained_and_well_formed) {
    const auto d = fixture();
    const auto html = renderHtml(d);
    CM_EXPECT(html.rfind("<!DOCTYPE html>\n<html lang=\"en\">", 0) == 0);
    CM_EXPECT(html.size() > 10 && html.compare(html.size() - 8, 8, "</html>\n") == 0);
    CM_EXPECT(validUtf8(html));
    CM_EXPECT(count(html, "<svg") == 1 && count(html, "</svg>") == 1);
    CM_EXPECT(count(html, "<table") == count(html, "</table>"));
    CM_EXPECT(count(html, "<section") == count(html, "</section>"));
    CM_EXPECT(count(html, "<tbody>") == count(html, "</tbody>"));
    CM_EXPECT(count(html, "<tr>") == count(html, "</tr>"));
    CM_EXPECT(count(html, "<g>") == count(html, "</g>"));
    CM_EXPECT(count(html, "<figure>") == 1 && count(html, "</figure>") == 1);
    // nothing external or executable
    for (const char* bad : { "<script", "http", " src=\"", " href=\"", "@import", "url(", "<link", "<iframe", "<object", "<img", "javascript:" })
        CM_EXPECT(!contains(html, bad));
    for (const char* id : { "id=\"summary\"", "id=\"timeline\"", "id=\"top-moments\"", "id=\"fx\"", "id=\"scenes\"", "id=\"feedback\"", "id=\"markers\"", "id=\"device\"" })
        CM_EXPECT(contains(html, id));
    CM_EXPECT(contains(html, "<title>Show Report - Midnight &lt;Tour&gt; &amp; Co</title>"));
    CM_EXPECT(contains(html, "prefers-color-scheme:dark"));
    CM_EXPECT(contains(html, "viewBox=\"0 0 960 260\""));
    CM_EXPECT(contains(html, ">1</text>") && contains(html, ">2</text>"));                // numbered top moments
    CM_EXPECT(contains(html, "Top moment #1: 0:00:09 (21:03:14), -33.1 dBFS, +12.4 dB"));
    CM_EXPECT(contains(html, "Top moment #2: 0:00:21 (21:03:26)"));
    CM_EXPECT(contains(html, "Clip 0:00:20") && contains(html, "Feedback candidate 0:00:09") && contains(html, "Panic 0:00:15") && contains(html, "Dropout (xrun) 0:00:18"));
    CM_EXPECT(contains(html, "Scene 0:00:13: Chorus &lt;b&gt;"));
    CM_EXPECT(contains(html, "2500 Hz") && contains(html, "83 %"));
    CM_EXPECT(contains(html, "not calibrated sound pressure"));
    CM_EXPECT(contains(html, "1 closer than the minimum spacing"));
    CM_EXPECT(html.size() < 60000);
}

CM_TEST(report_html_and_json_escape_every_user_string) {
    auto d = fixture();
    const std::string nasty = "a\x01" "b\xC3\x28" "c\xE2\x80\xAE" "d\"q\\";   // control char, invalid UTF-8, bidi override, quote, backslash
    d.meta.show = "<script>alert(1)</script>";
    d.meta.venue = "\"quoted\" & 'single'";
    d.meta.scene = "</title><script>x()</script>";
    d.meta.sessionFile = "<u>s</u>.json";
    d.meta.appVersion = "1.0<i>";
    d.meta.device.driver = "<drv>";
    d.meta.device.name = nasty;
    d.scenes[0].name = "</td><script>";
    d.sceneEvents[0].name = "</title><script>";
    d.markers[0].label = nasty;
    d.markers[1].label = "<img src=x onerror=alert(1)>";
    const auto html = renderHtml(d);
    CM_EXPECT(!contains(html, "<script") && !contains(html, "<img") && !contains(html, "<u>") && !contains(html, "<i>"));
    CM_EXPECT(contains(html, "&lt;script&gt;alert(1)&lt;/script&gt;"));
    CM_EXPECT(contains(html, "&quot;quoted&quot; &amp; &#39;single&#39;"));
    CM_EXPECT(contains(html, "&lt;img src=x onerror=alert(1)&gt;"));
    CM_EXPECT(!contains(html, std::string(1, '\x01')) && !contains(html, "\xE2\x80\xAE") && validUtf8(html));
    CM_EXPECT(contains(html, "ab\xEF\xBF\xBD(cd&quot;q\\"));

    const auto json = renderJson(d);
    CM_EXPECT(validUtf8(json));
    J root;
    CM_EXPECT(JsonParser::parse(json, root));
    CM_EXPECT(root.get("show")->get("name")->str == "<script>alert(1)</script>");   // JSON keeps text, only syntax-escapes it
    CM_EXPECT(root.get("device")->get("name")->str == "ab\xEF\xBF\xBD(cd\"q\\");
    CM_EXPECT(root.get("markers")->arr[0].get("label")->str == "ab\xEF\xBF\xBD(cd\"q\\");
    CM_EXPECT(!contains(json, std::string(1, '\x01')));

    CM_EXPECT(escapeHtml("a&b<c>d\"e'f") == "a&amp;b&lt;c&gt;d&quot;e&#39;f");
    CM_EXPECT(escapeHtml("line1\nline2\ttab") == "line1 line2 tab");
    CM_EXPECT(escapeJson("l1\nl2\t\"\\") == "l1\\nl2\\t\\\"\\\\");
    CM_EXPECT(sanitizeUtf8("ok \xC3\xA9 \xF0\x9F\x8E\xB8") == "ok \xC3\xA9 \xF0\x9F\x8E\xB8");   // valid text is untouched
    CM_EXPECT(sanitizeUtf8("\xC0\xAF") == "\xEF\xBF\xBD\xEF\xBF\xBD");                            // overlong encoding rejected
    CM_EXPECT(sanitizeUtf8("\xED\xA0\x80") == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");            // UTF-16 surrogate rejected
    CM_EXPECT(sanitizeUtf8(std::string("a\0b", 3)) == "ab");
}

CM_TEST(report_number_and_time_formatting) {
    CM_EXPECT(formatNumber(-0.001, 2) == "0.00");
    CM_EXPECT(formatNumber(std::nan(""), 2) == "0.00");
    CM_EXPECT(formatNumber(INFINITY, 1) == "0.0");
    CM_EXPECT(formatNumber(1234.56, 1) == "1234.6");
    CM_EXPECT(formatNumber(-27.8, 2) == "-27.80");
    CM_EXPECT(formatNumber(48000, 0) == "48000");
    CM_EXPECT(formatTimecode(0) == "0:00:00" && formatTimecode(3725) == "1:02:05" && formatTimecode(-4) == "0:00:00");
    CM_EXPECT(formatTimecode(59.6) == "0:01:00" && formatTimecode(std::nan("")) == "0:00:00" && formatTimecode(86399.4) == "23:59:59");
    CM_EXPECT(formatLocalIso(1792555385000LL, -420) == "2026-10-20T21:03:05-07:00");
    CM_EXPECT(formatLocalIso(1792555385000LL, 0) == "2026-10-21T04:03:05+00:00");
    CM_EXPECT(formatLocalIso(1709251199000LL, 0) == "2024-02-29T23:59:59+00:00");
    CM_EXPECT(formatLocalIso(1709251199000LL, 60) == "2024-03-01T00:59:59+01:00");      // day, month and leap-year rollover
    CM_EXPECT(formatLocalIso(1709251199000LL, 345) == "2024-03-01T05:44:59+05:45");     // half-hour offsets
    CM_EXPECT(formatLocalIso(946684800000LL, 0) == "2000-01-01T00:00:00+00:00");
    CM_EXPECT(formatLocalIso(-1000LL, 0) == "1969-12-31T23:59:59+00:00");
    CM_EXPECT(formatLocalDate(1792555385000LL, -420) == "2026-10-20" && formatLocalClock(1792555385000LL, -420) == "21:03:05");
}

CM_TEST(report_output_does_not_depend_on_the_global_locale) {
    const auto d = fixture();
    const auto csv = renderCsv(d), json = renderJson(d), html = renderHtml(d);
    std::locale previous;
    bool switched = false;
    for (const char* name : { "de_DE.UTF-8", "fr_FR.UTF-8", "de_DE.utf8", "de_DE" }) {
        try { std::locale::global(std::locale(name)); switched = true; break; } catch (const std::exception&) {}
    }
    const auto csv2 = renderCsv(d), json2 = renderJson(d), html2 = renderHtml(d);
    std::locale::global(previous);
    CM_EXPECT(csv == csv2 && json == json2 && html == html2);
    (void) switched;   // when no comma-decimal locale is installed the comparison is trivially true
}

CM_TEST(report_empty_and_long_show_documents_are_valid) {
    const ReportData empty;
    const auto html = renderHtml(empty);
    CM_EXPECT(contains(html, "No crowd peaks were detected.") && contains(html, "No scene was recalled.") && contains(html, "None detected."));
    CM_EXPECT(contains(html, "Untitled show") && contains(html, "no signal"));
    CM_EXPECT(count(html, "<svg") == 1 && validUtf8(html));
    J root;
    CM_EXPECT(JsonParser::parse(renderJson(empty), root));
    CM_EXPECT(root.get("series")->get("rows")->arr.empty() && root.get("peaks")->arr.empty());

    ReportData big;   // 12 h at 1 Hz
    for (int i = 0; i < 43200; ++i) {
        Sample s;
        s.t = i;
        s.crowdDb = static_cast<float>(-40.0 + 10.0 * std::sin(i * 0.01));
        s.masterDb = s.crowdDb + 3.0f;
        big.series.push_back(s);
    }
    big.summary.durationSeconds = 43200.0;
    const auto bigHtml = renderHtml(big);
    CM_EXPECT(bigHtml.size() < 120000);                                   // timeline is downsampled to <= 896 columns
    CM_EXPECT(contains(bigHtml, "time since start (h:mm)"));
    J bigRoot;
    CM_EXPECT(JsonParser::parse(renderJson(big), bigRoot) && bigRoot.get("series")->get("rows")->arr.size() == 43200);
    big.stepSeconds = 2.0;
    CM_EXPECT(contains(renderHtml(big), "Long show: one point every 2 s."));
}
