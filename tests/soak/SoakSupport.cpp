#include "SoakSupport.h"
#include <cstdio>
#include <new>

#if defined(__APPLE__)
 #include <mach/mach.h>
#endif

namespace soak {
namespace {
std::atomic<std::uint64_t> gAllocs { 0 }, gFrees { 0 };
thread_local std::uint64_t tlAllocs = 0;
}
AllocStats globalAllocStats() noexcept { return { gAllocs.load(std::memory_order_relaxed), gFrees.load(std::memory_order_relaxed) }; }
std::uint64_t& threadAllocCount() noexcept { return tlAllocs; }
namespace detail {
void* allocate(std::size_t n) {
    void* p = std::malloc(n != 0 ? n : 1);
    if (p == nullptr) throw std::bad_alloc();
    gAllocs.fetch_add(1, std::memory_order_relaxed);
    ++tlAllocs;
    return p;
}
void release(void* p) noexcept {
    if (p == nullptr) return;
    gFrees.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
}

std::int64_t residentBytes() {
#if defined(__linux__)
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (f == nullptr) return -1;
    long size = 0, resident = 0;
    const int n = std::fscanf(f, "%ld %ld", &size, &resident);
    std::fclose(f);
    return n == 2 ? (std::int64_t) resident * 4096 : -1;
#elif defined(__APPLE__)
    mach_task_basic_info info {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &count) != KERN_SUCCESS) return -1;
    return (std::int64_t) info.resident_size;
#else
    return -1;   // Windows: the allocation counters are the memory-growth signal there
#endif
}

void enableEverything(CrowdMikeAudioProcessor& p, int numInputs) {
    for (int i = 1; i <= numInputs && i <= 16; ++i) {
        const juce::String in = "input" + juce::String(i) + ".";
        for (int fx = 1; fx <= 7; ++fx) setParam(p, in + "fxSend" + juce::String(fx), 0.0f);
        setParam(p, in + "highPassEnabled", 1.0f);
        setParam(p, in + "lowPassEnabled", 1.0f);
        setParam(p, in + "notchOn", 1.0f);
        setParam(p, in + "phaseStages", 2.0f);
        setParam(p, in + "gainRiding", 1.0f);
    }
    for (int g = 1; g <= 8; ++g)
        for (int fx = 1; fx <= 7; ++fx) setParam(p, "group" + juce::String(g) + ".fxSend" + juce::String(fx), -6.0f);
    for (const char* m : { "fx.delay.", "fx.echo.", "fx.tape.", "fx.reverse.", "fx.noise." }) setParam(p, juce::String(m) + "bypass", 0.0f);
    setParam(p, "fx.delay.mode", 2.0f);          // multi-tap
    setParam(p, "fx.reverse.trigMode", 1.0f);    // repeating, so the reverse buffer keeps cycling
    setParam(p, "master.widthOn", 1.0f);
    setParam(p, "master.width", 1.5f);
    setParam(p, "master.decorr", 0.5f);
    // Modules added after the original five: reverb + multiplier returns, performer ducker, tempo grid with quantiser.
    setParam(p, "fx.reverb.on", 1.0f);
    setParam(p, "fx.multi.on", 1.0f);
    setParam(p, "duckOn", 1.0f);
    setParam(p, "tempo.source", 2.0f);      // Manual
    setParam(p, "tempo.quantise", 1.0f);    // 1/4
    setParam(p, "fx.reverb.preSync", 1.0f);
}
} // namespace soak

// ---- global allocation accounting (this executable only) -------------------------------------------------------------
void* operator new(std::size_t n) { return soak::detail::allocate(n); }
void* operator new[](std::size_t n) { return soak::detail::allocate(n); }
void operator delete(void* p) noexcept { soak::detail::release(p); }
void operator delete[](void* p) noexcept { soak::detail::release(p); }
void operator delete(void* p, std::size_t) noexcept { soak::detail::release(p); }
void operator delete[](void* p, std::size_t) noexcept { soak::detail::release(p); }
