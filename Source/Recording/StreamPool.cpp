#include "StreamPool.h"
#include <algorithm>
#include <cstring>

namespace crowdmike::rec {

class StreamPool::Thread : public juce::Thread {
public:
    explicit Thread(StreamPool& o) : juce::Thread("CrowdMike clip streamer"), owner(o) {}
    void run() override {
        while (! threadShouldExit()) {
            owner.pump();
            wait(2);
        }
    }
private:
    StreamPool& owner;
};

StreamPool::StreamPool() = default;
StreamPool::~StreamPool() { stop(); }

void StreamPool::prepare(double sr, int block) {
    stop();
    std::lock_guard<std::mutex> l(pumpLock);
    sampleRate = sr > 0.0 ? sr : 48000.0;
    maxBlock = std::max(1, block);
    engineRateForSources = sampleRate;
    int cap = juce::nextPowerOfTwo((int) std::ceil(sampleRate * kRingSeconds));
    cap = std::max(cap, juce::nextPowerOfTwo(kFillChunk + 4 * maxBlock));
    capacity = cap;
    sources.clear();
    rings.clear();
    for (int i = 0; i < kRings; ++i) {
        auto r = std::make_unique<Ring>();
        r->L.calloc((size_t) cap);
        r->R.calloc((size_t) cap);
        r->capacity = cap;
        r->mask = cap - 1;
        rings.push_back(std::move(r));
    }
    for (auto& s : scratch) s.assign((size_t) kFillChunk, 0.0f);
}

void StreamPool::start(bool startThread) {
    if (thread != nullptr || ! startThread) return;
    thread = std::make_unique<Thread>(*this);
    thread->startThread();
}

void StreamPool::stop() {
    if (thread != nullptr) {
        thread->signalThreadShouldExit();
        thread->notify();
        thread->stopThread(5000);
        thread.reset();
    }
    std::lock_guard<std::mutex> l(pumpLock);
    for (auto& r : rings) {
        r->state.store(Free);
        r->src = nullptr;
    }
    sources.clear();
}

juce::uint64 StreamPool::registerSource(const juce::File& f) {
    std::lock_guard<std::mutex> l(regLock);
    const auto id = nextId++;
    registry[id] = f;
    return id;
}

void StreamPool::unregisterSource(juce::uint64 id) {
    if (id == 0) return;
    {
        std::lock_guard<std::mutex> l(regLock);
        registry.erase(id);
    }
    registryDirty.store(true);
}

int StreamPool::acquire(juce::uint64 sourceId, juce::int64 regionStart, juce::int64 regionLen, bool reverse, bool loop,
                        juce::uint64 startSeq) noexcept {
    for (size_t i = 0; i < rings.size(); ++i) {
        auto& r = *rings[i];
        if (r.state.load(std::memory_order_acquire) != Free) continue;
        r.sourceId = sourceId;
        r.regionStart = regionStart;
        r.regionLen = regionLen;
        r.reverse = reverse;
        r.loop.store(loop, std::memory_order_relaxed);
        r.consumeSeq.store(startSeq, std::memory_order_relaxed);
        r.writeSeq.store(startSeq, std::memory_order_relaxed);
        r.state.store(Requested, std::memory_order_release);
        return (int) i;
    }
    starvedAcquires.fetch_add(1, std::memory_order_relaxed);
    return -1;
}

void StreamPool::release(int i) noexcept {
    if (i >= 0 && (size_t) i < rings.size()) rings[(size_t) i]->state.store(Releasing, std::memory_order_release);
}

StreamSource* StreamPool::sourceFor(juce::uint64 id) {
    if (auto it = sources.find(id); it != sources.end()) return it->second.get();
    juce::File f;
    {
        std::lock_guard<std::mutex> l(regLock);
        if (auto it = registry.find(id); it != registry.end()) f = it->second;
        else return nullptr;
    }
    auto s = std::make_unique<StreamSource>();
    juce::String err;
    if (! s->open(f, engineRateForSources, err)) {
        readErrors.fetch_add(1);
        return nullptr;
    }
    auto* p = s.get();
    sources[id] = std::move(s);
    return p;
}

bool StreamPool::fillOne(Ring& r) {
    const auto cs = r.consumeSeq.load(std::memory_order_acquire);
    auto ws = r.writeSeq.load(std::memory_order_relaxed);
    if (ws < cs) {   // the audio thread overtook the producer (underrun): skip ahead, stay in time
        ws = cs;
        r.writeSeq.store(ws, std::memory_order_release);
    }
    const juce::uint64 limit = cs + (juce::uint64) (r.capacity - 2 * maxBlock);
    if (ws + (juce::uint64) kFillChunk > limit) return false;
    const bool loop = r.loop.load(std::memory_order_relaxed);
    int produced = 0;
    if (r.src != nullptr) {
        const auto f0 = r.src->failedReads;
        produced = r.src->readSequence(r.regionStart, r.regionLen, r.reverse, loop, ws, kFillChunk,
                                       scratch[0].data(), scratch[1].data());
        if (r.src->failedReads != f0) readErrors.fetch_add(1);
    } else if (loop || ws < (juce::uint64) r.regionLen) {
        // source unavailable (file removed / unreadable): keep time by producing silence
        produced = (int) (loop ? (juce::int64) kFillChunk : std::min<juce::int64>(kFillChunk, r.regionLen - (juce::int64) ws));
        std::fill(scratch[0].begin(), scratch[0].begin() + produced, 0.0f);
        std::fill(scratch[1].begin(), scratch[1].begin() + produced, 0.0f);
    }
    if (produced <= 0) return false;
    const int off = (int) (ws & (juce::uint64) r.mask);
    const int first = std::min(produced, r.capacity - off);
    std::memcpy(r.L.get() + off, scratch[0].data(), sizeof(float) * (size_t) first);
    std::memcpy(r.R.get() + off, scratch[1].data(), sizeof(float) * (size_t) first);
    if (first < produced) {
        std::memcpy(r.L.get(), scratch[0].data() + first, sizeof(float) * (size_t) (produced - first));
        std::memcpy(r.R.get(), scratch[1].data() + first, sizeof(float) * (size_t) (produced - first));
    }
    r.writeSeq.store(ws + (juce::uint64) produced, std::memory_order_release);
    return produced == kFillChunk;
}

void StreamPool::pump() {
    std::lock_guard<std::mutex> l(pumpLock);
    if (rings.empty()) return;
    for (auto& rp : rings) {
        auto& r = *rp;
        const int s = r.state.load(std::memory_order_acquire);
        if (s == Releasing) {
            r.src = nullptr;
            r.state.store(Free, std::memory_order_release);
        } else if (s == Requested) {
            r.src = sourceFor(r.sourceId);
            if (r.src == nullptr) readErrors.fetch_add(1);
            int expected = Requested;
            r.state.compare_exchange_strong(expected, Active, std::memory_order_acq_rel);
        }
    }
    for (int round = 0; round < 32; ++round) {
        bool any = false;
        for (auto& rp : rings)
            if (rp->state.load(std::memory_order_acquire) == Active) any = fillOne(*rp) || any;
        if (! any) break;
    }
    if (registryDirty.exchange(false)) {   // drop readers of unregistered clips that no ring uses any more
        std::vector<juce::uint64> dead;
        {
            std::lock_guard<std::mutex> rl(regLock);
            for (auto& [id, s] : sources)
                if (registry.find(id) == registry.end()) dead.push_back(id);
        }
        for (auto id : dead) {
            bool used = false;
            for (auto& rp : rings)
                used = used || (rp->state.load() != Free && rp->state.load() != Releasing && rp->sourceId == id);
            if (used) registryDirty.store(true);
            else sources.erase(id);
        }
    }
}

StreamPool::Stats StreamPool::getStats() const {
    Stats s;
    s.underrunEvents = underrunEvents.load();
    s.underrunFrames = underrunFrames.load();
    s.starvedAcquires = starvedAcquires.load();
    s.readErrors = readErrors.load();
    s.ringCount = (int) rings.size();
    for (auto& r : rings) {
        if (r->state.load() == Active || r->state.load() == Requested) ++s.activeRings;
        s.ringBytes += (juce::int64) r->capacity * 2 * (juce::int64) sizeof(float);
    }
    return s;
}

} // namespace crowdmike::rec
