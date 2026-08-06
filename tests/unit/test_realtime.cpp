// Realtime-safety guard: process() must never allocate on the audio thread.
// We override global new/delete in this test binary and count allocations
// made while a flag is set.

#include <atomic>
#include <cstdlib>
#include <new>

static std::atomic<bool> g_countingAllocations{false};
static std::atomic<long> g_allocationCount{0};

void* operator new(std::size_t size)
{
    if (g_countingAllocations.load(std::memory_order_relaxed))
        g_allocationCount.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "sapp/sounds/DiagnosticInstrument.h"

using namespace sapp::sounds;
using namespace sapptest;

TEST_CASE("process() performs zero heap allocations", "[realtime]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeDiagnosticInstrument({48000, 0.6f, 0.3f, 7}));

    std::vector<float> left(512, 0.0f), right(512, 0.0f);

    // Warm-up block adopts the pending instrument snapshot.
    engine.process(nullptr, 0, left.data(), right.data(), 512);

    // A busy musical block: chords, keyswitch, pedal, releases, bends.
    std::vector<MidiEvent> events{
        noteOn(0, 12, 100),           // keyswitch → sustain (looped, keeps sounding)
        noteOn(4, 48, 110), noteOn(4, 55, 96), noteOn(4, 64, 120),
        controller(8, 64, 127),       // pedal down
        noteOff(64, 48),
        controller(128, 64, 0),       // pedal up
        noteOn(200, 72, 80),
    };
    MidiEvent bend;
    bend.type = MidiEvent::Type::PitchBend;
    bend.frame = 300;
    bend.bend14 = 4096;
    events.push_back(bend);
    std::sort(events.begin(), events.end(),
              [](const MidiEvent& a, const MidiEvent& b) { return a.frame < b.frame; });

    g_allocationCount.store(0);
    g_countingAllocations.store(true);
    for (int block = 0; block < 200; ++block) {
        engine.process(block == 0 ? events.data() : nullptr,
                       block == 0 ? int(events.size()) : 0,
                       left.data(), right.data(), 512);
    }
    g_countingAllocations.store(false);

    CHECK(g_allocationCount.load() == 0);
    CHECK(engine.activeVoiceCount() > 0);
}

TEST_CASE("instrument swap on the audio thread does not allocate", "[realtime]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    auto instA = makeDiagnosticInstrument({48000, 0.5f, 0.3f, 1});
    auto instB = makeSineInstrument();
    engine.setInstrument(instA);

    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    engine.process(nullptr, 0, left.data(), right.data(), 512);
    std::vector<MidiEvent> on{noteOn(0, 60, 100)};
    engine.process(on.data(), 1, left.data(), right.data(), 512);

    // Stage the swap from the control side (allocations allowed here)...
    engine.setInstrument(instB);

    // ...then the audio thread adopts it allocation-free.
    g_allocationCount.store(0);
    g_countingAllocations.store(true);
    for (int block = 0; block < 50; ++block)
        engine.process(nullptr, 0, left.data(), right.data(), 512);
    g_countingAllocations.store(false);

    CHECK(g_allocationCount.load() == 0);
    engine.collectRetired();  // control thread reclaims the old snapshot
}
