#pragma once
// Show Report writers (SHOW_REPORT_SPEC section 9): pure functions ReportData -> text. No JUCE, no I/O, locale independent.
#include <cstdint>
#include <string>
#include "ReportTypes.h"

namespace crowdmike::report {
// ---- text helpers (exposed for tests)
std::string sanitizeUtf8(const std::string& s);              // invalid UTF-8 -> U+FFFD, control chars (except \n \t) removed
std::string escapeHtml(const std::string& s);                // sanitize + & < > " '
std::string escapeJson(const std::string& s);                // sanitize + RFC 8259 escapes (no surrounding quotes)
std::string formatNumber(double v, int decimals);            // classic locale, non-finite -> 0, no "-0.00"
std::string formatTimecode(double seconds);                  // h:mm:ss
std::string formatLocalIso(std::int64_t epochMs, int utcOffsetMinutes);     // 2026-10-20T21:03:05-07:00
std::string formatLocalClock(std::int64_t epochMs, int utcOffsetMinutes);   // 21:03:05
std::string formatLocalDate(std::int64_t epochMs, int utcOffsetMinutes);    // 2026-10-20

// ---- documents
std::string renderHtml(const ReportData& d);
std::string renderCsv(const ReportData& d);
std::string renderJson(const ReportData& d);

extern const char* const kCsvHeader;
extern const char* const kJsonColumns;   // JSON array text of the series column names
}
