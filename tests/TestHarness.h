#pragma once
// Minimal self-registering test harness (no external dependency, runs under ctest).
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace cmtest {
struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline const char*& currentTest() { static const char* n = ""; return n; }
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({ n, std::move(f) }); } };
inline void expectImpl(bool ok, const char* expr, const char* file, int line) {
    if (!ok) { std::cerr << "FAIL [" << currentTest() << "] " << file << ":" << line << "  " << expr << '\n'; ++failures(); }
}
inline void nearImpl(double a, double b, double tol, const char* ea, const char* eb, const char* file, int line) {
    if (!(std::fabs(a - b) <= tol)) {
        std::cerr << "FAIL [" << currentTest() << "] " << file << ":" << line << "  " << ea << " (" << a << ") != " << eb
                  << " (" << b << ") +/- " << tol << '\n';
        ++failures();
    }
}
}
#define CM_TEST(name) static void name(); static cmtest::Registrar cmReg_##name(#name, name); static void name()
#define CM_EXPECT(cond) cmtest::expectImpl(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define CM_EXPECT_NEAR(a, b, tol) cmtest::nearImpl((a), (b), (tol), #a, #b, __FILE__, __LINE__)
