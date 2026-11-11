#include "TestHarness.h"
#include "Standalone/MidiEventQueue.h"
#include <atomic>
#include <thread>
#include <vector>

using namespace crowdmike;

namespace {
using Queue = MidiEventQueue<64>;

juce::MidiMessage cc(int number, int value, double timeSeconds) {
    return juce::MidiMessage::controllerEvent(1, number, value).withTimeStamp(timeSeconds);
}

std::vector<juce::MidiMessage> drained(juce::MidiBuffer& buffer) {
    std::vector<juce::MidiMessage> out;
    for (const auto meta : buffer) out.push_back(meta.getMessage());
    return out;
}
}

CM_TEST(midi_queue_delivers_short_messages_in_order_at_block_positions) {
    Queue q;
    juce::MidiBuffer buffer;
    Queue::reserve(buffer);
    const double sr = 48000.0, t0 = 100.0;
    q.reset(sr, t0);
    CM_EXPECT(q.push(cc(1, 10, t0 + 48.0 / sr)));                       // 48 samples after the previous drain
    CM_EXPECT(q.push(juce::MidiMessage::noteOn(1, 60, 1.0f).withTimeStamp(t0 + 100.0 / sr)));
    CM_EXPECT(q.push(juce::MidiMessage::midiClock().withTimeStamp(t0 - 1.0)));   // earlier than the window: clamped to 0
    CM_EXPECT(q.push(cc(2, 20, t0 + 10.0)));                             // far later than the block: clamped to the end

    const int block = 512;
    CM_EXPECT(q.drainInto(buffer, block, t0 + block / sr) == 4);   // exactly one block elapsed: positions map 1:1
    const auto events = drained(buffer);
    CM_EXPECT(events.size() == 4);
    if (events.size() == 4) {
        CM_EXPECT(events[0].isMidiClock());
        CM_EXPECT(events[1].isController() && events[1].getControllerNumber() == 1);
        CM_EXPECT(events[2].isNoteOn());
        CM_EXPECT(events[3].isController() && events[3].getControllerNumber() == 2);
    }
    std::vector<int> positions;
    for (const auto meta : buffer) positions.push_back(meta.samplePosition);
    CM_EXPECT(positions == std::vector<int>({ 0, 48, 100, block - 1 }));
    CM_EXPECT(q.droppedOverflow() == 0 && q.rejectedLongMessages() == 0);

    buffer.clear();
    CM_EXPECT(q.drainInto(buffer, block, t0 + 2 * block / sr) == 0);   // nothing left behind
    CM_EXPECT(buffer.isEmpty());
}

CM_TEST(midi_queue_map_to_block_aligns_or_compresses_the_elapsed_window) {
    // Window shorter than the block: shifted so that it ends at the block end.
    CM_EXPECT(Queue::mapToBlock(0, 100, 512) == 412);
    CM_EXPECT(Queue::mapToBlock(99, 100, 512) == 511);
    CM_EXPECT(Queue::mapToBlock(-5, 100, 512) == 407);
    // Window equal to the block: identity.
    CM_EXPECT(Queue::mapToBlock(7, 512, 512) == 7);
    // Window longer than the block (a late callback): compressed proportionally, still in order.
    CM_EXPECT(Queue::mapToBlock(0, 2048, 512) == 0);
    CM_EXPECT(Queue::mapToBlock(1024, 2048, 512) == 256);
    CM_EXPECT(Queue::mapToBlock(2047, 2048, 512) == 511);
    // Always inside the block.
    CM_EXPECT(Queue::mapToBlock(-100000, 1, 64) == 0);
    CM_EXPECT(Queue::mapToBlock(100000000LL, 1, 64) == 63);
    CM_EXPECT(Queue::mapToBlock(5, 0, 64) == 63);
    CM_EXPECT(Queue::mapToBlock(5, 64, 0) == 0);
}

CM_TEST(midi_queue_rejects_long_messages_and_counts_overflow) {
    Queue q;
    q.reset(48000.0, 1.0);
    const uint8_t sysex[] = { 0x7D, 1, 2, 3, 4, 5 };
    CM_EXPECT(!q.push(juce::MidiMessage::createSysExMessage(sysex, 6).withTimeStamp(1.0)));
    CM_EXPECT(q.rejectedLongMessages() == 1);
    CM_EXPECT(q.droppedOverflow() == 0);

    for (int i = 0; i < static_cast<int>(Queue::kCapacity); ++i) CM_EXPECT(q.push(cc(1, i & 127, 1.0)));
    CM_EXPECT(!q.push(cc(1, 0, 1.0)));          // full: dropped, never blocks
    CM_EXPECT(!q.push(cc(1, 0, 1.0)));
    CM_EXPECT(q.droppedOverflow() == 2);

    juce::MidiBuffer buffer;
    Queue::reserve(buffer);
    CM_EXPECT(q.drainInto(buffer, 256, 1.0 + 256.0 / 48000.0) == static_cast<int>(Queue::kCapacity));
    CM_EXPECT(buffer.getNumEvents() == static_cast<int>(Queue::kCapacity));
    int expected = 0;
    for (const auto meta : buffer) { CM_EXPECT(meta.getMessage().getControllerValue() == (expected & 127)); ++expected; }

    // After a drain the slots are free again.
    CM_EXPECT(q.push(cc(1, 5, 1.1)));
    buffer.clear();
    CM_EXPECT(q.drainInto(buffer, 256, 1.2) == 1);
}

CM_TEST(midi_queue_reset_discards_stale_events_and_restarts_the_time_base) {
    Queue q;
    q.reset(48000.0, 1.0);
    for (int i = 0; i < 10; ++i) CM_EXPECT(q.push(cc(1, i, 1.0)));
    q.reset(96000.0, 50.0);                        // device restarted while events were pending
    juce::MidiBuffer buffer;
    Queue::reserve(buffer);
    CM_EXPECT(q.drainInto(buffer, 128, 50.0 + 128.0 / 96000.0) == 0);
    CM_EXPECT(q.push(cc(1, 7, 50.0 + 128.0 / 96000.0 + 64.0 / 96000.0)));   // 64 samples into the next block
    CM_EXPECT(q.drainInto(buffer, 128, 50.0 + 256.0 / 96000.0) == 1);
    CM_EXPECT(buffer.getNumEvents() == 1);
    for (const auto meta : buffer) CM_EXPECT(meta.samplePosition == 64);
}

CM_TEST(midi_queue_multiple_producers_keep_per_producer_order_against_a_draining_consumer) {
    MidiEventQueue<256> q;
    q.reset(48000.0, 0.0);
    constexpr int producers = 4, perProducer = 5000;
    std::atomic<bool> go { false };
    std::atomic<int> finished { 0 };
    std::array<std::atomic<int>, producers> pushed {};
    std::vector<std::thread> threads;
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&, p] {
            while (!go.load()) std::this_thread::yield();
            for (int i = 0; i < perProducer; ++i) {
                // channel = producer; a 14-bit sequence number split over controller number/value keeps order checkable
                const auto m = juce::MidiMessage::controllerEvent(p + 1, (i >> 7) & 127, i & 127).withTimeStamp(1.0 + i * 1.0e-6);
                if (q.push(m)) pushed[static_cast<size_t>(p)].fetch_add(1);
                else std::this_thread::yield();   // dropped on overflow: the test only requires order and exact delivery
            }
            finished.fetch_add(1);
        });
    }
    juce::MidiBuffer buffer;
    MidiEventQueue<256>::reserve(buffer);
    std::array<int, producers> lastSeen; lastSeen.fill(-1);
    std::array<long, producers> received {};
    bool ordered = true;
    double now = 0.0;
    const auto drainOnce = [&] {
        buffer.clear();
        now += 64.0 / 48000.0;
        q.drainInto(buffer, 64, now);
        for (const auto meta : buffer) {
            const auto m = meta.getMessage();
            const auto p = static_cast<size_t>(m.getChannel() - 1);
            const int seq = (m.getControllerNumber() << 7) | m.getControllerValue();
            if (seq <= lastSeen[p]) ordered = false;
            lastSeen[p] = seq;
            ++received[p];
        }
    };
    go.store(true);
    while (finished.load() < producers) drainOnce();
    for (auto& t : threads) t.join();
    for (int k = 0; k < 8; ++k) drainOnce();   // whatever the last producers left behind

    CM_EXPECT(ordered);
    long totalPushed = 0, totalReceived = 0;
    for (int p = 0; p < producers; ++p) { totalPushed += pushed[static_cast<size_t>(p)].load(); totalReceived += received[static_cast<size_t>(p)]; }
    CM_EXPECT(totalReceived == totalPushed);   // every accepted event arrives exactly once
    CM_EXPECT(totalPushed + static_cast<long>(q.droppedOverflow()) == static_cast<long>(producers) * perProducer);
    CM_EXPECT(totalPushed > 0);
}
