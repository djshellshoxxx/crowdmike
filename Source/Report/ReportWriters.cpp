#include "ReportWriters.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <sstream>

namespace crowdmike::report {
const char* const kCsvHeader = "time_s,crowd_db,density,master_db,master_peak_db,lufs_s,clip,limiter,feedback,panic,fx,scene,xrun,recording";
const char* const kJsonColumns = "[\"t\",\"crowdDb\",\"density\",\"masterDb\",\"masterPeakDb\",\"lufs\",\"clip\",\"limiter\",\"feedback\",\"panic\",\"fx\",\"scene\",\"xrun\",\"recording\"]";

// ====================================================================================================== text helpers
std::string sanitizeUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    const auto replacement = [&out] { out += "\xEF\xBF\xBD"; };
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            if (c >= 0x20 && c != 0x7F) out += static_cast<char>(c);
            else if (c == '\n' || c == '\t') out += static_cast<char>(c);
            ++i;
            continue;
        }
        int len = 0;
        std::uint32_t cp = 0;
        if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1Fu; }
        else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0Fu; }
        else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07u; }
        bool ok = len > 0 && i + static_cast<std::size_t>(len) <= s.size();
        for (int k = 1; ok && k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            ok = (cc & 0xC0) == 0x80;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (ok) {
            if (len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ok = false;
            if (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ok = false;
        }
        if (!ok) { replacement(); ++i; continue; }
        const bool c1 = cp >= 0x80 && cp <= 0x9F;
        const bool bidi = (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069) || cp == 0x200E || cp == 0x200F;
        if (!c1 && !bidi) out.append(s, i, static_cast<std::size_t>(len));
        i += static_cast<std::size_t>(len);
    }
    return out;
}

std::string escapeHtml(const std::string& s) {
    const std::string clean = sanitizeUtf8(s);
    std::string out;
    out.reserve(clean.size() + 8);
    for (const char c : clean) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            case '\n': case '\t': out += ' '; break;
            default: out += c;
        }
    }
    return out;
}

std::string escapeJson(const std::string& s) {
    const std::string clean = sanitizeUtf8(s);
    std::string out;
    out.reserve(clean.size() + 8);
    for (const char c : clean) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string formatNumber(double v, int decimals) {
    if (!std::isfinite(v)) v = 0.0;
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::fixed << std::setprecision(std::max(0, decimals)) << v;
    std::string r = os.str();
    if (!r.empty() && r[0] == '-' && r.find_first_not_of("-0.") == std::string::npos) r.erase(0, 1);   // no "-0.00"
    return r;
}

namespace {
std::string num(double v, int d) { return formatNumber(v, d); }
std::string integer(long long v) { return std::to_string(v); }

std::string pad2(long long v) { return (v < 10 ? "0" : "") + std::to_string(v); }

// Howard Hinnant's civil_from_days.
void civilFromDays(long long z, long long& y, int& m, int& d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y += (m <= 2);
}

struct Civil { long long y = 1970; int mo = 1, d = 1, h = 0, mi = 0, s = 0; };
Civil toCivil(std::int64_t epochMs, int offsetMin) {
    const long long ms = static_cast<long long>(epochMs) + static_cast<long long>(offsetMin) * 60000ll;
    long long secs = ms / 1000;
    if (ms % 1000 < 0) --secs;
    long long days = secs / 86400;
    long long rem = secs % 86400;
    if (rem < 0) { rem += 86400; --days; }
    Civil c;
    civilFromDays(days, c.y, c.mo, c.d);
    c.h = static_cast<int>(rem / 3600);
    c.mi = static_cast<int>((rem % 3600) / 60);
    c.s = static_cast<int>(rem % 60);
    return c;
}
}

std::string formatTimecode(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const long long t = static_cast<long long>(seconds + 0.5);
    return std::to_string(t / 3600) + ":" + pad2((t % 3600) / 60) + ":" + pad2(t % 60);
}

std::string formatLocalDate(std::int64_t epochMs, int off) {
    const auto c = toCivil(epochMs, off);
    return std::to_string(c.y) + "-" + pad2(c.mo) + "-" + pad2(c.d);
}
std::string formatLocalClock(std::int64_t epochMs, int off) {
    const auto c = toCivil(epochMs, off);
    return pad2(c.h) + ":" + pad2(c.mi) + ":" + pad2(c.s);
}
std::string formatLocalIso(std::int64_t epochMs, int off) {
    const int a = off < 0 ? -off : off;
    return formatLocalDate(epochMs, off) + "T" + formatLocalClock(epochMs, off) + (off < 0 ? "-" : "+") + pad2(a / 60) + ":" + pad2(a % 60);
}

// ====================================================================================================== CSV
std::string renderCsv(const ReportData& d) {
    std::string out = kCsvHeader;
    out += '\n';
    for (const auto& s : d.series) {
        out += num(s.t, 1); out += ',';
        out += num(s.crowdDb, 2); out += ',';
        out += num(s.density, 3); out += ',';
        out += num(s.masterDb, 2); out += ',';
        out += num(s.masterPeakDb, 2); out += ',';
        out += num(s.lufs, 2); out += ',';
        out += integer(s.clip); out += ',';
        out += integer(s.limiter); out += ',';
        out += integer(s.feedback); out += ',';
        out += integer(s.panic); out += ',';
        out += integer(s.fx); out += ',';
        out += integer(s.scene); out += ',';
        out += integer(s.xrun); out += ',';
        out += s.recording ? '1' : '0';
        out += '\n';
    }
    return out;
}

// ====================================================================================================== JSON
namespace {
std::string q(const std::string& s) { return "\"" + escapeJson(s) + "\""; }
std::string boolean(bool b) { return b ? "true" : "false"; }

std::string peakJson(const PeakEvent& p) {
    return "{\"timeSeconds\": " + num(p.t, 1) + ", \"levelDb\": " + num(p.levelDb, 2) + ", \"prominenceDb\": " + num(p.prominenceDb, 2)
         + ", \"marker\": " + boolean(p.marker) + "}";
}

template <typename T, typename F>
void jsonArray(std::string& out, const char* key, const std::vector<T>& v, F&& item, bool last = false) {
    out += "  \"" + std::string(key) + "\": [";
    for (std::size_t i = 0; i < v.size(); ++i) {
        out += i == 0 ? "\n    " : ",\n    ";
        out += item(v[i]);
    }
    out += v.empty() ? "]" : "\n  ]";
    out += last ? "\n" : ",\n";
}
}

std::string renderJson(const ReportData& d) {
    const auto& m = d.meta;
    const auto& s = d.summary;
    std::string o = "{\n";
    o += "  \"schema\": \"crowdmike.show-report\",\n";
    o += "  \"schemaVersion\": " + integer(kSchemaVersion) + ",\n";
    o += "  \"generator\": {\"app\": \"CrowdMike\", \"version\": " + q(m.appVersion) + "},\n";
    const std::int64_t endMs = m.startEpochMs + static_cast<std::int64_t>(std::llround(s.durationSeconds * 1000.0));
    o += "  \"show\": {\"name\": " + q(m.show) + ", \"venue\": " + q(m.venue) + ", \"scene\": " + q(m.scene)
       + ", \"start\": " + q(formatLocalIso(m.startEpochMs, m.utcOffsetMinutes)) + ", \"end\": " + q(formatLocalIso(endMs, m.utcOffsetMinutes))
       + ", \"durationSeconds\": " + num(s.durationSeconds, 1) + "},\n";
    o += "  \"session\": {\"file\": " + q(m.sessionFile) + ", \"take\": " + integer(m.take) + "},\n";
    o += "  \"device\": {\"driver\": " + q(m.device.driver) + ", \"name\": " + q(m.device.name) + ", \"sampleRate\": " + num(m.device.sampleRate, 0)
       + ", \"bufferSize\": " + integer(m.device.bufferSize) + ", \"latencySamples\": " + integer(m.device.latencySamples)
       + ", \"latencyMs\": " + num(m.device.latencyMs, 2) + ", \"inputs\": " + integer(m.device.inputs) + ", \"outputs\": " + integer(m.device.outputs) + "},\n";
    o += "  \"settings\": {\"peakSensitivity\": " + num(m.peakSensitivity, 2) + ", \"peakProminenceDb\": " + num(m.peakProminenceDb, 2)
       + ", \"peakMinSpacingSeconds\": " + num(m.peakMinSpacingSeconds, 1) + ", \"autoMarkers\": " + boolean(m.autoMarkers) + "},\n";
    o += "  \"summary\": {\"averageCrowdDb\": " + num(s.averageCrowdDb, 2) + ", \"peakCrowdDb\": " + num(s.peakCrowdDb, 2)
       + ", \"peakCrowdAtSeconds\": " + num(s.peakCrowdAtSeconds, 1) + ", \"peakCount\": " + integer(s.peakCount)
       + ", \"peaksSuppressed\": " + integer(s.peaksSuppressed) + ", \"maxShortTermLufs\": " + num(s.maxLufs, 2)
       + ", \"meanShortTermLufs\": " + num(s.meanLufs, 2) + ", \"maxMasterPeakDb\": " + num(s.maxMasterPeakDb, 2)
       + ", \"recordedSeconds\": " + num(s.recordedSeconds, 1) + ", \"clipEvents\": " + integer(s.clipEvents)
       + ", \"limiterEvents\": " + integer(s.limiterEvents) + ", \"feedbackEvents\": " + integer(s.feedbackEvents)
       + ", \"panicEvents\": " + integer(s.panicEvents) + ", \"xruns\": " + integer(s.xruns) + ", \"sceneRecalls\": " + integer(s.sceneRecalls)
       + ", \"fx\": {\"echoThrows\": " + integer(s.fx.echoThrows) + ", \"reverseTriggers\": " + integer(s.fx.reverseTriggers)
       + ", \"freezes\": " + integer(s.fx.freezes) + ", \"total\": " + integer(s.fx.total()) + "}},\n";
    jsonArray(o, "topMoments", d.topMoments, [&](const TopMoment& t) {
        return "{\"rank\": " + integer(t.rank) + ", \"timeSeconds\": " + num(t.peak.t, 1) + ", \"timecode\": " + q(formatTimecode(t.peak.t))
             + ", \"clock\": " + q(formatLocalClock(m.startEpochMs + static_cast<std::int64_t>(std::llround(t.peak.t * 1000.0)), m.utcOffsetMinutes))
             + ", \"levelDb\": " + num(t.peak.levelDb, 2) + ", \"prominenceDb\": " + num(t.peak.prominenceDb, 2) + "}";
    });
    jsonArray(o, "peaks", d.peaks, [](const PeakEvent& p) { return peakJson(p); });
    jsonArray(o, "scenes", d.scenes, [](const SceneUse& u) {
        return "{\"name\": " + q(u.name) + ", \"recalls\": " + integer(u.recalls) + ", \"firstSeconds\": " + num(u.firstSeconds, 1) + "}";
    });
    jsonArray(o, "feedbackEvents", d.feedbackEvents, [](const FeedbackEvent& f) {
        return "{\"timeSeconds\": " + num(f.t, 1) + ", \"frequencyHz\": " + num(f.frequencyHz, 1) + ", \"severity\": " + num(f.severity, 2) + "}";
    });
    jsonArray(o, "markers", d.markers, [](const MarkerEvent& k) { return "{\"timeSeconds\": " + num(k.t, 1) + ", \"label\": " + q(k.label) + "}"; });
    o += "  \"series\": {\n    \"stepSeconds\": " + num(d.stepSeconds, 0) + ",\n    \"columns\": " + kJsonColumns + ",\n    \"rows\": [";
    for (std::size_t i = 0; i < d.series.size(); ++i) {
        const auto& r = d.series[i];
        o += i == 0 ? "\n      [" : ",\n      [";
        o += num(r.t, 1) + ", " + num(r.crowdDb, 2) + ", " + num(r.density, 3) + ", " + num(r.masterDb, 2) + ", " + num(r.masterPeakDb, 2) + ", "
           + num(r.lufs, 2) + ", " + integer(r.clip) + ", " + integer(r.limiter) + ", " + integer(r.feedback) + ", " + integer(r.panic) + ", "
           + integer(r.fx) + ", " + integer(r.scene) + ", " + integer(r.xrun) + ", " + (r.recording ? "1" : "0") + "]";
    }
    o += d.series.empty() ? "]\n  }\n}\n" : "\n    ]\n  }\n}\n";
    return o;
}

// ====================================================================================================== HTML
namespace {
const char* const kCss =
    ":root{--bg:#f6f7f9;--card:#fff;--ink:#14181d;--muted:#5b6672;--line:#d9dee4;--crowd:#c2410c;--crowdfill:rgba(194,65,12,.16);"
    "--master:#2563eb;--accent:#0f766e;--warn:#b45309;--bad:#b91c1c;--grid:#e5e8ec}"
    "@media (prefers-color-scheme:dark){:root{--bg:#0f1317;--card:#171c22;--ink:#e8ecf0;--muted:#98a4b0;--line:#2a323b;--crowd:#fb923c;"
    "--crowdfill:rgba(251,146,60,.18);--master:#60a5fa;--accent:#2dd4bf;--warn:#fbbf24;--bad:#f87171;--grid:#222a32}}"
    "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}"
    "main{max-width:1000px;margin:0 auto;padding:24px 16px 48px}h1{margin:0;font-size:28px;letter-spacing:-.01em}"
    "h2{margin:32px 0 10px;font-size:17px}.sub{margin:4px 0 0;font-size:18px}.meta{margin:2px 0 0;color:var(--muted);font-size:14px}"
    ".cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;margin-top:18px}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px}"
    ".card b{display:block;font-size:26px;line-height:1.2;font-variant-numeric:tabular-nums}.card span{color:var(--muted);font-size:13px}"
    "table{width:100%;border-collapse:collapse;background:var(--card);border:1px solid var(--line);border-radius:10px;overflow:hidden}"
    "th,td{padding:7px 12px;text-align:left;border-bottom:1px solid var(--line);font-variant-numeric:tabular-nums}"
    "th{font-size:12px;text-transform:uppercase;letter-spacing:.04em;color:var(--muted);font-weight:600}tr:last-child td{border-bottom:0}"
    "td.n,th.n{text-align:right}.none{color:var(--muted);margin:0}figure{margin:0;background:var(--card);border:1px solid var(--line);"
    "border-radius:10px;padding:8px}figcaption{color:var(--muted);font-size:13px;padding:4px 6px}svg{display:block;width:100%;height:auto}"
    ".grid{stroke:var(--grid);stroke-width:1}.axis{fill:var(--muted);font-size:11px}.crowd{fill:none;stroke:var(--crowd);stroke-width:1.6}"
    ".crowdfill{fill:var(--crowdfill);stroke:none}.master{fill:none;stroke:var(--master);stroke-width:1;stroke-dasharray:4 3}"
    ".peak{fill:var(--crowd);stroke:var(--ink);stroke-width:.8}.peak2{fill:none;stroke:var(--crowd);stroke-width:1.4}"
    ".rank{fill:var(--ink);font-size:11px;font-weight:700;text-anchor:middle}.mk{stroke:var(--accent);stroke-width:1;stroke-dasharray:3 3}"
    ".mkd{fill:var(--accent)}.rec{fill:var(--bad);opacity:.55}.ev-scene{fill:var(--accent)}.ev-clip{fill:var(--bad)}"
    ".ev-fb{fill:var(--warn)}.ev-panic{fill:var(--bad)}.ev-xrun{fill:var(--muted)}"
    "footer{margin-top:32px;color:var(--muted);font-size:13px}@media print{body{background:#fff}.card,table,figure{break-inside:avoid}}";

struct Plot {
    double x0 = 52, x1 = 948, y0 = 14, y1 = 184;
    double hi = 0, lo = -60;
    double duration = 1;
    double X(double t) const { return x0 + std::min(std::max(t / duration, 0.0), 1.0) * (x1 - x0); }
    double Y(double db) const { return y1 - std::min(std::max((db - lo) / (hi - lo), 0.0), 1.0) * (y1 - y0); }
};

std::string tick(double seconds, bool hours) {
    const long long t = static_cast<long long>(seconds + 0.5);
    if (hours) return std::to_string(t / 3600) + ":" + pad2((t % 3600) / 60);
    return std::to_string(t / 60) + ":" + pad2(t % 60);
}

std::string svgTimeline(const ReportData& d) {
    const auto& m = d.meta;
    Plot p;
    p.duration = std::max({ d.summary.durationSeconds, d.stepSeconds, 1.0 });
    double maxV = -1.0e9;
    for (const auto& s : d.series) maxV = std::max({ maxV, static_cast<double>(s.crowdDb), static_cast<double>(s.masterDb) });
    if (maxV < -100.0) maxV = -20.0;
    p.hi = std::min(6.0, std::max(-6.0, std::ceil((maxV + 2.0) / 6.0) * 6.0));
    p.lo = p.hi - 60.0;

    std::string o;
    o += "<svg viewBox=\"0 0 960 260\" role=\"img\" aria-labelledby=\"tl-t tl-d\">";
    o += "<title id=\"tl-t\">Crowd energy timeline</title><desc id=\"tl-d\">Crowd level in dBFS over time with peaks, markers, scene changes and events.</desc>";
    for (int g = 0; g <= 5; ++g) {
        const double db = p.hi - 12.0 * g;
        const double y = p.Y(db);
        o += "<line class=\"grid\" x1=\"" + num(p.x0, 0) + "\" x2=\"" + num(p.x1, 0) + "\" y1=\"" + num(y, 1) + "\" y2=\"" + num(y, 1) + "\"/>";
        o += "<text class=\"axis\" x=\"" + num(p.x0 - 6, 0) + "\" y=\"" + num(y + 4, 1) + "\" text-anchor=\"end\">" + num(db, 0) + "</text>";
    }
    o += "<text class=\"axis\" x=\"4\" y=\"10\">dBFS</text>";

    // recording spans
    {
        std::size_t i = 0;
        while (i < d.series.size()) {
            if (!d.series[i].recording) { ++i; continue; }
            std::size_t j = i;
            while (j + 1 < d.series.size() && d.series[j + 1].recording) ++j;
            const double xa = p.X(d.series[i].t), xb = p.X(d.series[j].t + d.stepSeconds);
            o += "<rect class=\"rec\" x=\"" + num(xa, 1) + "\" y=\"212\" width=\"" + num(std::max(2.0, xb - xa), 1) + "\" height=\"6\"><title>Recording "
               + formatTimecode(d.series[i].t) + " to " + formatTimecode(d.series[j].t + d.stepSeconds) + "</title></rect>";
            i = j + 1;
        }
    }

    // series (max per column, at most 896 columns)
    if (!d.series.empty()) {
        const std::size_t n = d.series.size();
        const std::size_t cols = std::min<std::size_t>(n, 896);
        std::string line, mline;
        double firstX = 0, lastX = 0;
        for (std::size_t c = 0; c < cols; ++c) {
            const std::size_t a = c * n / cols, b = std::max(a + 1, (c + 1) * n / cols);
            float cm = kFloorDb, mm = kFloorDb;
            for (std::size_t k = a; k < b && k < n; ++k) { cm = std::max(cm, d.series[k].crowdDb); mm = std::max(mm, d.series[k].masterDb); }
            const double x = p.X(d.series[a].t + 0.5 * d.stepSeconds);
            if (c == 0) firstX = x;
            lastX = x;
            line += (c == 0 ? "M" : "L") + num(x, 1) + " " + num(p.Y(cm), 1);
            mline += (c == 0 ? "M" : "L") + num(x, 1) + " " + num(p.Y(mm), 1);
        }
        o += "<path class=\"crowdfill\" d=\"" + line + "L" + num(lastX, 1) + " " + num(p.y1, 1) + "L" + num(firstX, 1) + " " + num(p.y1, 1) + "Z\"/>";
        o += "<path class=\"master\" d=\"" + mline + "\"/>";
        o += "<path class=\"crowd\" d=\"" + line + "\"/>";
    }

    // user markers
    for (std::size_t i = 0; i < d.markers.size() && i < 200; ++i) {
        const auto& k = d.markers[i];
        const double x = p.X(k.t);
        o += "<g><title>Marker " + formatTimecode(k.t) + ": " + escapeHtml(k.label) + "</title><line class=\"mk\" x1=\"" + num(x, 1) + "\" x2=\"" + num(x, 1)
           + "\" y1=\"" + num(p.y0, 0) + "\" y2=\"" + num(p.y1, 0) + "\"/><path class=\"mkd\" d=\"M" + num(x, 1) + " " + num(p.y0 - 2, 1) + "l4 4l-4 4l-4 -4z\"/></g>";
    }

    // peaks (top-5 are filled and numbered)
    for (const auto& pk : d.peaks) {
        int rank = 0;
        for (const auto& t : d.topMoments) if (t.peak.t == pk.t && t.peak.prominenceDb == pk.prominenceDb) rank = t.rank;
        const double x = p.X(pk.t), y = p.Y(pk.levelDb);
        const std::string title = (rank > 0 ? "Top moment #" + integer(rank) + ": " : std::string("Peak: ")) + formatTimecode(pk.t) + " ("
            + formatLocalClock(m.startEpochMs + static_cast<std::int64_t>(std::llround(pk.t * 1000.0)), m.utcOffsetMinutes) + "), " + num(pk.levelDb, 1)
            + " dBFS, +" + num(pk.prominenceDb, 1) + " dB";
        const std::string tri = "M" + num(x - 6, 1) + " " + num(y - 16, 1) + "L" + num(x + 6, 1) + " " + num(y - 16, 1) + "L" + num(x, 1) + " " + num(y - 5, 1) + "Z";
        o += "<g><title>" + title + "</title><path class=\"" + (rank > 0 ? "peak" : "peak2") + "\" d=\"" + tri + "\"/>";
        if (rank > 0) o += "<text class=\"rank\" x=\"" + num(x, 1) + "\" y=\"" + num(y - 20, 1) + "\">" + integer(rank) + "</text>";
        o += "</g>";
    }

    // event row (shape + title, never colour only): scene = bar, clip = square, feedback = circle, panic = diamond, xrun = triangle
    o += "<text class=\"axis\" x=\"4\" y=\"202\">events</text>";
    int drawn = 0;
    for (const auto& e : d.sceneEvents) {
        if (drawn++ > 300) break;
        const double x = p.X(e.t);
        o += "<g><title>Scene " + formatTimecode(e.t) + ": " + escapeHtml(e.name) + "</title><rect class=\"ev-scene\" x=\"" + num(x - 1, 1) + "\" y=\"190\" width=\"2\" height=\"14\"/></g>";
    }
    drawn = 0;
    for (const auto& s : d.series) {
        if (drawn > 400) break;
        const double x = p.X(s.t);
        const std::string at = formatTimecode(s.t);
        if (s.clip > 0) { ++drawn; o += "<g><title>Clip " + at + " x" + integer(s.clip) + "</title><rect class=\"ev-clip\" x=\"" + num(x - 3, 1) + "\" y=\"192\" width=\"6\" height=\"6\"/></g>"; }
        if (s.feedback > 0) { ++drawn; o += "<g><title>Feedback candidate " + at + "</title><circle class=\"ev-fb\" cx=\"" + num(x, 1) + "\" cy=\"198\" r=\"3.5\"/></g>"; }
        if (s.panic > 0) { ++drawn; o += "<g><title>Panic " + at + "</title><path class=\"ev-panic\" d=\"M" + num(x, 1) + " 191l4 5l-4 5l-4 -5z\"/></g>"; }
        if (s.xrun > 0) { ++drawn; o += "<g><title>Dropout (xrun) " + at + " x" + integer(s.xrun) + "</title><path class=\"ev-xrun\" d=\"M" + num(x, 1) + " 192l4 8h-8z\"/></g>"; }
    }

    // time axis
    static const double steps[] = { 10, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400, 21600, 43200, 86400 };
    double stepT = steps[13];
    for (const double st : steps) if (p.duration / st <= 10.0) { stepT = st; break; }
    const bool hours = p.duration >= 3600.0;
    for (double t = 0.0; t <= p.duration + 1e-6; t += stepT) {
        o += "<text class=\"axis\" x=\"" + num(p.X(t), 1) + "\" y=\"240\" text-anchor=\"middle\">" + tick(t, hours) + "</text>";
    }
    o += "<text class=\"axis\" x=\"" + num(p.x1, 0) + "\" y=\"254\" text-anchor=\"end\">time since start (" + std::string(hours ? "h:mm" : "m:ss") + ")</text>";
    o += "</svg>";
    return o;
}

std::string row(const std::string& label, const std::string& value) {
    return "<tr><th scope=\"row\">" + label + "</th><td>" + value + "</td></tr>";
}
std::string dbText(float db) { return db <= kFloorDb + 0.01f ? std::string("no signal") : num(db, 1) + " dBFS"; }
std::string lufsText(float v) { return v <= kFloorDb + 0.01f ? std::string("no signal") : num(v, 1) + " LUFS"; }
}

std::string renderHtml(const ReportData& d) {
    const auto& m = d.meta;
    const auto& s = d.summary;
    const std::string showName = m.show.empty() ? std::string("Untitled show") : m.show;
    const std::int64_t endMs = m.startEpochMs + static_cast<std::int64_t>(std::llround(s.durationSeconds * 1000.0));
    const auto clockAt = [&](double t) { return formatLocalClock(m.startEpochMs + static_cast<std::int64_t>(std::llround(t * 1000.0)), m.utcOffsetMinutes); };

    std::string o;
    o += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n";
    o += "<title>Show Report - " + escapeHtml(showName) + "</title>\n<style>" + kCss + "</style>\n</head>\n<body>\n<main>\n";
    o += "<header>\n<h1>Show Report</h1>\n<p class=\"sub\">" + escapeHtml(showName) + (m.venue.empty() ? "" : " &middot; " + escapeHtml(m.venue)) + "</p>\n";
    o += "<p class=\"meta\">" + formatLocalDate(m.startEpochMs, m.utcOffsetMinutes) + ", " + formatLocalClock(m.startEpochMs, m.utcOffsetMinutes) + " to "
       + formatLocalClock(endMs, m.utcOffsetMinutes) + " (UTC" + (m.utcOffsetMinutes < 0 ? "-" : "+") + pad2(std::abs(m.utcOffsetMinutes) / 60) + ":"
       + pad2(std::abs(m.utcOffsetMinutes) % 60) + ") &middot; " + formatTimecode(s.durationSeconds) + "</p>\n</header>\n";

    o += "<section id=\"summary\" aria-label=\"Summary\">\n<div class=\"cards\">\n";
    o += "<div class=\"card\"><b>" + formatTimecode(s.durationSeconds) + "</b><span>Duration</span></div>\n";
    o += "<div class=\"card\"><b>" + dbText(s.averageCrowdDb) + "</b><span>Average crowd level</span></div>\n";
    o += "<div class=\"card\"><b>" + dbText(s.peakCrowdDb) + "</b><span>Peak crowd level at " + formatTimecode(s.peakCrowdAtSeconds) + "</span></div>\n";
    o += "<div class=\"card\"><b>" + integer(s.peakCount) + "</b><span>Crowd peaks detected</span></div>\n</div>\n";
    o += "<h2>Summary</h2>\n<table>\n<tbody>\n";
    o += row("Duration", formatTimecode(s.durationSeconds)) + "\n";
    o += row("Recorded", formatTimecode(s.recordedSeconds)) + "\n";
    o += row("Average crowd level", dbText(s.averageCrowdDb)) + "\n";
    o += row("Peak crowd level", dbText(s.peakCrowdDb) + " at " + formatTimecode(s.peakCrowdAtSeconds) + " (" + clockAt(s.peakCrowdAtSeconds) + ")") + "\n";
    o += row("Crowd peaks", integer(s.peakCount) + (s.peaksSuppressed > 0 ? " (" + integer(s.peaksSuppressed) + " closer than the minimum spacing, not counted)" : std::string())) + "\n";
    o += row("Loudness, short-term (approx.)", "max " + lufsText(s.maxLufs) + ", mean " + lufsText(s.meanLufs)) + "\n";
    o += row("Highest master peak", dbText(s.maxMasterPeakDb)) + "\n";
    o += row("Clip events", integer(s.clipEvents)) + "\n";
    o += row("Limiter events", integer(s.limiterEvents)) + "\n";
    o += row("Feedback candidates", integer(s.feedbackEvents)) + "\n";
    o += row("Panic mutes", integer(s.panicEvents)) + "\n";
    o += row("Audio dropouts (xruns)", integer(s.xruns)) + "\n";
    o += row("Scene recalls", integer(s.sceneRecalls)) + "\n";
    o += row("Scene at start", m.scene.empty() ? std::string("none") : escapeHtml(m.scene)) + "\n";
    o += "</tbody>\n</table>\n</section>\n";

    o += "<section id=\"timeline\" aria-label=\"Energy timeline\">\n<h2>Energy timeline</h2>\n<figure>\n" + svgTimeline(d)
       + "\n<figcaption>Solid line: crowd level. Dashed line: master level. Numbered triangles: top-5 moments; hollow triangles: other peaks; "
         "diamonds and dashed lines: markers; bars: scene changes; square: clip; circle: feedback candidate; diamond: panic; triangle: dropout. "
         "Red band: recording.";
    if (d.stepSeconds > 1.0) o += " Long show: one point every " + num(d.stepSeconds, 0) + " s.";
    o += "</figcaption>\n</figure>\n</section>\n";

    o += "<section id=\"top-moments\" aria-label=\"Top moments\">\n<h2>Top moments</h2>\n";
    if (d.topMoments.empty()) o += "<p class=\"none\">No crowd peaks were detected.</p>\n";
    else {
        o += "<table>\n<thead><tr><th>#</th><th>Time</th><th>Clock</th><th class=\"n\">Level</th><th class=\"n\">Rise</th></tr></thead>\n<tbody>\n";
        for (const auto& t : d.topMoments)
            o += "<tr><td>" + integer(t.rank) + "</td><td>" + formatTimecode(t.peak.t) + "</td><td>" + clockAt(t.peak.t) + "</td><td class=\"n\">" + num(t.peak.levelDb, 1)
               + " dBFS</td><td class=\"n\">+" + num(t.peak.prominenceDb, 1) + " dB</td></tr>\n";
        o += "</tbody>\n</table>\n";
    }
    o += "</section>\n";

    o += "<section id=\"fx\" aria-label=\"Effects usage\">\n<h2>Effects used</h2>\n<table>\n<tbody>\n";
    o += row("Echo throws", integer(s.fx.echoThrows)) + "\n" + row("Reverse triggers", integer(s.fx.reverseTriggers)) + "\n"
       + row("Freeze / hold", integer(s.fx.freezes)) + "\n</tbody>\n</table>\n</section>\n";

    o += "<section id=\"scenes\" aria-label=\"Scenes used\">\n<h2>Scenes used</h2>\n";
    if (d.scenes.empty()) o += "<p class=\"none\">No scene was recalled.</p>\n";
    else {
        o += "<table>\n<thead><tr><th>Scene</th><th class=\"n\">Recalls</th><th>First used</th></tr></thead>\n<tbody>\n";
        for (const auto& u : d.scenes)
            o += "<tr><td>" + escapeHtml(u.name) + "</td><td class=\"n\">" + integer(u.recalls) + "</td><td>" + formatTimecode(u.firstSeconds) + "</td></tr>\n";
        o += "</tbody>\n</table>\n";
    }
    o += "</section>\n";

    o += "<section id=\"feedback\" aria-label=\"Feedback candidates\">\n<h2>Feedback candidates</h2>\n";
    if (d.feedbackEvents.empty()) o += "<p class=\"none\">None detected.</p>\n";
    else {
        o += "<table>\n<thead><tr><th>Time</th><th class=\"n\">Frequency</th><th class=\"n\">Severity</th></tr></thead>\n<tbody>\n";
        for (const auto& f : d.feedbackEvents)
            o += "<tr><td>" + formatTimecode(f.t) + "</td><td class=\"n\">" + num(f.frequencyHz, 0) + " Hz</td><td class=\"n\">" + num(f.severity * 100.0, 0) + " %</td></tr>\n";
        o += "</tbody>\n</table>\n";
    }
    o += "</section>\n";

    if (!d.markers.empty()) {
        o += "<section id=\"markers\" aria-label=\"Markers\">\n<h2>Markers</h2>\n<table>\n<thead><tr><th>Time</th><th>Label</th></tr></thead>\n<tbody>\n";
        for (const auto& k : d.markers) o += "<tr><td>" + formatTimecode(k.t) + "</td><td>" + escapeHtml(k.label) + "</td></tr>\n";
        o += "</tbody>\n</table>\n</section>\n";
    }

    o += "<section id=\"device\" aria-label=\"Audio setup\">\n<h2>Audio setup</h2>\n<table>\n<tbody>\n";
    o += row("Driver", m.device.driver.empty() ? std::string("n/a") : escapeHtml(m.device.driver)) + "\n";
    o += row("Device", m.device.name.empty() ? std::string("n/a") : escapeHtml(m.device.name)) + "\n";
    o += row("Sample rate", m.device.sampleRate > 0 ? num(m.device.sampleRate, 0) + " Hz" : std::string("n/a")) + "\n";
    o += row("Buffer size", m.device.bufferSize > 0 ? integer(m.device.bufferSize) + " samples" : std::string("n/a")) + "\n";
    o += row("Estimated latency", m.device.latencySamples > 0 ? integer(m.device.latencySamples) + " samples (" + num(m.device.latencyMs, 1) + " ms)" : std::string("n/a")) + "\n";
    o += row("Channels", integer(m.device.inputs) + " in, " + integer(m.device.outputs) + " out") + "\n";
    o += row("Crowd peak detection", "rise " + num(m.peakProminenceDb, 1) + " dB, spacing " + num(m.peakMinSpacingSeconds, 0) + " s, sensitivity "
             + num(m.peakSensitivity * 100.0, 0) + " %, recorder markers " + (m.autoMarkers ? "on" : "off")) + "\n";
    if (!m.sessionFile.empty()) o += row("Session", escapeHtml(m.sessionFile) + (m.take > 0 ? " (take " + integer(m.take) + ")" : std::string())) + "\n";
    o += "</tbody>\n</table>\n</section>\n";

    o += "<footer>Generated by CrowdMike " + escapeHtml(m.appVersion) + ", report schema " + integer(kSchemaVersion)
       + ". Levels are digital dBFS measured on the CrowdMike crowd bus, not calibrated sound pressure (dB SPL). "
         "Loudness is an approximate K-weighted short-term value without gating.</footer>\n</main>\n</body>\n</html>\n";
    return o;
}
}
