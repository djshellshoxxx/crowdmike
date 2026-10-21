#include "TestHarness.h"
#include "Core/ParamTables.h"
#include "DSP/Ducker.h"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace crowdmike;
using namespace crowdmike::dsp;
namespace D = crowdmike::params::DuckP;

namespace {
struct DuckParams {
    std::array<float, D::Count> v {};
    DuckParams() {
        for (int k = 0; k < D::Count; ++k) v[static_cast<size_t>(k)] = params::GlobalP::defs[params::duckBase() + k].def;
        v[D::On] = 1.0f;
        v[D::Key] = 1.0f;
    }
    float& operator[](int k) { return v[static_cast<size_t>(k)]; }
    const float* data() const { return v.data(); }
};

// Key: `lead` seconds of silence, `burst` seconds of a sine, `tail` seconds of silence.
std::vector<float> makeKey(double sr, double lead, double burst, double tail, float amp = 0.316f, double hz = 1000.0) {
    const int nl = static_cast<int>(lead * sr), nb = static_cast<int>(burst * sr), nt = static_cast<int>(tail * sr);
    std::vector<float> k(static_cast<size_t>(nl + nb + nt), 0.0f);
    for (int i = 0; i < nb; ++i) k[static_cast<size_t>(nl + i)] = amp * static_cast<float>(std::sin(6.283185307179586 * hz * i / sr));
    return k;
}

std::vector<float> render(double sr, int block, const DuckParams& p, const std::vector<float>& key, bool enabled = true) {
    Ducker d;
    d.prepare(sr, block);
    std::vector<float> g(key.size());
    for (size_t o = 0; o < key.size(); o += static_cast<size_t>(block)) {
        const int n = static_cast<int>(std::min<size_t>(static_cast<size_t>(block), key.size() - o));
        d.process(key.data() + o, n, p.data(), enabled, g.data() + o);
    }
    return g;
}
double toDb(float g) { return 20.0 * std::log10(std::max(g, 1.0e-9f)); }
// First index >= from where the reduction magnitude reaches `db` (reduction positive).
int firstAtLeast(const std::vector<float>& g, int from, double db) {
    for (size_t i = static_cast<size_t>(from); i < g.size(); ++i) if (-toDb(g[i]) >= db) return static_cast<int>(i);
    return -1;
}
// First index >= from where the reduction falls to <= db.
int firstAtMost(const std::vector<float>& g, int from, double db) {
    for (size_t i = static_cast<size_t>(from); i < g.size(); ++i) if (-toDb(g[i]) <= db) return static_cast<int>(i);
    return -1;
}
constexpr double kRates[] = { 44100.0, 48000.0, 96000.0 };
}

CM_TEST(ducker_attack_time_matches_formula) {
    for (double sr : kRates) for (float attack : { 5.0f, 20.0f, 80.0f }) {
        DuckParams p;
        p[D::Depth] = -12.0f; p[D::Attack] = attack; p[D::Thr] = -35.0f; p[D::Hold] = 1000.0f; p[D::Release] = 500.0f;
        const auto key = makeKey(sr, 0.1, 1.5, 0.2);
        const auto g = render(sr, 64, p, key);
        const int start = static_cast<int>(0.1 * sr);
        const int i10 = firstAtLeast(g, start, 1.2), i90 = firstAtLeast(g, start, 10.8), full = firstAtLeast(g, start, 11.999);
        CM_EXPECT(i10 > 0 && i90 > 0 && full > 0);
        const double ms = 1000.0 / sr;
        CM_EXPECT_NEAR((i90 - i10) * ms, 0.8 * attack, 1.0);
        CM_EXPECT_NEAR((full - start) * ms, attack, 1.0);
    }
}

CM_TEST(ducker_release_time_matches_formula) {
    for (double sr : kRates) for (float release : { 50.0f, 400.0f, 1500.0f }) {
        DuckParams p;
        p[D::Depth] = -12.0f; p[D::Attack] = 5.0f; p[D::Hold] = 0.0f; p[D::Release] = release;
        const auto key = makeKey(sr, 0.1, 0.4, release / 1000.0 + 0.6);
        const auto g = render(sr, 128, p, key);
        const int end = static_cast<int>(0.5 * sr);
        const int top = firstAtMost(g, end, 11.99);                      // release has begun
        const int i90 = firstAtMost(g, end, 10.8), i10 = firstAtMost(g, end, 1.2), done = firstAtMost(g, end, 0.0);
        CM_EXPECT(top > 0 && i90 > 0 && i10 > 0 && done > 0);
        const double ms = 1000.0 / sr;
        CM_EXPECT_NEAR((i10 - i90) * ms, 0.8 * release, 1.0);
        CM_EXPECT_NEAR((done - top) * ms, release, 1.5);
    }
}

CM_TEST(ducker_hold_delays_release) {
    const double sr = 48000.0;
    int start[2] = { 0, 0 };
    int k = 0;
    for (float hold : { 0.0f, 100.0f }) {
        DuckParams p;
        p[D::Depth] = -12.0f; p[D::Attack] = 5.0f; p[D::Hold] = hold; p[D::Release] = 200.0f;
        const auto g = render(sr, 64, p, makeKey(sr, 0.1, 0.4, 1.0));
        start[k++] = firstAtMost(g, static_cast<int>(0.5 * sr), 11.99);
    }
    CM_EXPECT_NEAR((start[1] - start[0]) * 1000.0 / sr, 100.0, 1.0);
}

CM_TEST(ducker_depth_accuracy_and_range_clamp) {
    const double sr = 48000.0;
    for (float depth : { -3.0f, -6.0f, -12.0f, -30.0f }) {
        DuckParams p;
        p[D::Depth] = depth; p[D::Attack] = 5.0f;
        const auto g = render(sr, 256, p, makeKey(sr, 0.05, 0.6, 0.0));
        CM_EXPECT_NEAR(toDb(g[g.size() - 1]), depth, 0.01);
        for (float v : g) CM_EXPECT(v <= 1.0f && v > 0.0f && std::isfinite(v));
    }
    DuckParams p;
    p[D::Depth] = -20.0f; p[D::Range] = -8.0f; p[D::Attack] = 5.0f;
    const auto g = render(sr, 256, p, makeKey(sr, 0.05, 0.6, 0.0));
    CM_EXPECT_NEAR(toDb(g.back()), -8.0, 0.01);
    DuckParams z;
    z[D::Depth] = 0.0f;
    Ducker d;
    d.prepare(sr, 256);
    std::vector<float> out(4800);
    const auto key = makeKey(sr, 0.0, 0.1, 0.0);
    CM_EXPECT(!d.process(key.data(), 4800, z.data(), true, out.data()));
    CM_EXPECT(d.isOpen());
    for (float v : out) CM_EXPECT(v == 1.0f);
}

CM_TEST(ducker_threshold_and_hysteresis) {
    const double sr = 48000.0;
    DuckParams p;
    p[D::Thr] = -30.0f; p[D::Hold] = 0.0f; p[D::Attack] = 5.0f; p[D::Release] = 50.0f;
    p[D::KeyHpf] = 20.0f; p[D::KeyLpf] = 20000.0f;
    const auto level = [&](double db) { return static_cast<float>(std::pow(10.0, db / 20.0)); };
    // 1 dB under the threshold never opens, 1 dB over does.
    for (double db : { -31.0, -29.0 }) {
        const auto g = render(sr, 128, p, makeKey(sr, 0.0, 0.5, 0.0, level(db)));
        CM_EXPECT((db > -30.0) == (g.back() < 1.0f));
    }
    // Open with a loud burst, then hold the key between Thr-3 and Thr: it must stay open.
    std::vector<float> key = makeKey(sr, 0.0, 0.3, 0.0, level(-10.0));
    const auto mid = makeKey(sr, 0.0, 0.5, 0.0, level(-31.5));          // within the 3 dB hysteresis window
    key.insert(key.end(), mid.begin(), mid.end());
    const auto g = render(sr, 128, p, key);
    CM_EXPECT_NEAR(toDb(g.back()), -6.0, 0.01);
    // Below Thr - 3 it closes.
    const auto low = makeKey(sr, 0.0, 0.5, 0.0, level(-36.0));
    key.insert(key.end(), low.begin(), low.end());
    const auto g2 = render(sr, 128, p, key);
    CM_EXPECT(g2.back() == 1.0f);
}

CM_TEST(ducker_key_filters_shape_detection) {
    const double sr = 48000.0;
    const auto open = [&](double hz, float hpf, float lpf) {
        DuckParams p;
        p[D::KeyHpf] = hpf; p[D::KeyLpf] = lpf; p[D::Thr] = -30.0f;
        const auto g = render(sr, 128, p, makeKey(sr, 0.0, 0.6, 0.0, 0.05f, hz));
        return g.back() < 1.0f;
    };
    CM_EXPECT(!open(40.0, 120.0f, 6000.0f));
    CM_EXPECT(open(1000.0, 120.0f, 6000.0f));
    CM_EXPECT(!open(15000.0, 120.0f, 6000.0f));
    CM_EXPECT(open(15000.0, 120.0f, 20000.0f));
}

CM_TEST(ducker_deterministic_and_block_size_independent) {
    const double sr = 48000.0;
    DuckParams p;
    p[D::Depth] = -9.0f; p[D::Attack] = 7.0f; p[D::Release] = 90.0f; p[D::Hold] = 30.0f;
    auto key = makeKey(sr, 0.05, 0.3, 0.1);
    const auto k2 = makeKey(sr, 0.02, 0.2, 0.3, 0.1f, 440.0);
    key.insert(key.end(), k2.begin(), k2.end());
    const auto ref = render(sr, 1, p, key);
    for (int block : { 16, 64, 512, 2048 }) {
        const auto g = render(sr, block, p, key);
        bool same = g.size() == ref.size();
        for (size_t i = 0; same && i < g.size(); ++i) same = g[i] == ref[i];
        CM_EXPECT(same);
    }
    Ducker d;
    d.prepare(sr, 256);
    std::vector<float> a(key.size()), b(key.size());
    d.process(key.data(), static_cast<int>(key.size()), p.data(), true, a.data());
    d.reset();
    d.process(key.data(), static_cast<int>(key.size()), p.data(), true, b.data());
    CM_EXPECT(a == b);
}

CM_TEST(ducker_disabled_is_unity_and_releases_smoothly) {
    const double sr = 48000.0;
    DuckParams p;
    p[D::Release] = 100.0f; p[D::Depth] = -12.0f; p[D::Attack] = 5.0f;
    Ducker d;
    d.prepare(sr, 256);
    const auto key = makeKey(sr, 0.0, 0.4, 0.0);
    std::vector<float> g(key.size());
    d.process(key.data(), static_cast<int>(key.size()), p.data(), true, g.data());
    CM_EXPECT_NEAR(d.getGainReductionDb(), 12.0, 0.01);
    // disable: gain must climb back with no step larger than depth/release per sample (+ epsilon)
    std::vector<float> g2(static_cast<size_t>(0.3 * sr));
    d.process(key.data(), static_cast<int>(g2.size()), p.data(), false, g2.data());
    double prev = toDb(g.back());
    const double maxStep = 12.0 / (0.1 * sr) * 1.01 + 1e-6;
    for (float v : g2) { const double now = toDb(v); CM_EXPECT(now - prev <= maxStep); prev = now; }
    CM_EXPECT(g2.back() == 1.0f);
    CM_EXPECT(d.getGainReductionDb() == 0.0f);
    std::vector<float> g3(256);
    CM_EXPECT(!d.process(key.data(), 256, p.data(), false, g3.data()));
    for (float v : g3) CM_EXPECT(v == 1.0f);
    CM_EXPECT(!d.process(key.data(), 256, nullptr, true, g3.data()));
}

CM_TEST(ducker_survives_non_finite_and_extreme_params) {
    const double sr = 96000.0;
    Ducker d;
    d.prepare(sr, 2048);
    DuckParams p;
    p[D::Attack] = 1.0f; p[D::Release] = 10.0f; p[D::Hold] = 0.0f; p[D::KeyHpf] = 2000.0f; p[D::KeyLpf] = 1000.0f; p[D::Thr] = -80.0f;
    std::vector<float> key(2048), g(2048);
    for (int i = 0; i < 2048; ++i)
        key[static_cast<size_t>(i)] = (i % 97 == 0) ? std::numeric_limits<float>::quiet_NaN()
                                    : ((i % 53 == 0) ? std::numeric_limits<float>::infinity() : 0.9f * std::sin(0.3f * static_cast<float>(i)));
    for (int pass = 0; pass < 5; ++pass) {
        d.process(key.data(), 2048, p.data(), true, g.data());
        for (float v : g) CM_EXPECT(std::isfinite(v) && v > 0.0f && v <= 1.0f);
    }
    p[D::KeyHpf] = std::numeric_limits<float>::quiet_NaN();
    d.process(key.data(), 2048, p.data(), true, g.data());
    for (float v : g) CM_EXPECT(std::isfinite(v) && v > 0.0f && v <= 1.0f);
}

CM_TEST(ducker_param_block_is_contiguous_in_global_table) {
    using namespace crowdmike::params;
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::On].suffix, "duckOn") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::Key].suffix, "duckKey") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::ExcludeKey].suffix, "duckExcludeKey") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::Thr].suffix, "duckThrDb") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::Depth].suffix, "duckDepthDb") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::Attack].suffix, "duckAttackMs") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::Hold].suffix, "duckHoldMs") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::Release].suffix, "duckReleaseMs") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::Target].suffix, "duckTarget") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::Range].suffix, "duckRangeDb") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::KeyHpf].suffix, "duckKeyHpfHz") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::KeyLpf].suffix, "duckKeyLpfHz") == 0);
    CM_EXPECT(std::strcmp(GlobalP::defs[duckBase() + D::Lookahead].suffix, "duckLookaheadMs") == 0);
    CM_EXPECT(GlobalP::Count == duckBase() + D::Count);
    // pre-existing global IDs keep their position
    CM_EXPECT(std::strcmp(GlobalP::defs[GlobalP::ScenePrev].suffix, "scenePrev") == 0);
    CM_EXPECT(duckBase() == GlobalP::ScenePrev + 1);
}
