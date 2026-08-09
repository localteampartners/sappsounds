// Stuck-note regression suite.
//
// Root cause (fixed in PlaybackEngine.cpp): a stolen voice parks its new note
// in a PendingStart that only fires when the ~3 ms steal fade completes, but
// note-off delivery walked State::Active voices only. A note-off arriving
// inside the steal-fade window was lost and the pending note then played,
// held, forever. Measured from sappkeys (512-frame blocks, offs in the same
// block as the ons): N=50 simultaneous notes left 4 voices stuck at 10 s,
// N=120 left all 96 stuck. Hearing-safety issue downstream (sapptune #17).
//
// These tests reproduce that exact scenario against the diagnostic
// instrument's looped sustain articulation (a stuck voice loops audibly
// forever) and pin the fix: zero active voices at 10 s for every N.

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "sapp/sounds/DiagnosticInstrument.h"

using namespace sapp::sounds;
using namespace sapptest;

namespace {

// Keys 21..99 all have sustain regions (roots 24..96 step 7, ±3 spread).
uint8_t coveredKey(int i) { return uint8_t(21 + i % 79); }

// N note-ons at frame 0 and their note-offs at frame `offFrame` of the same
// 512-frame block — offs land inside the 3 ms (144-frame) steal-fade window.
std::vector<MidiEvent> burst(int n, uint32_t offFrame)
{
    std::vector<MidiEvent> events;
    for (int i = 0; i < n; ++i) events.push_back(noteOn(0, coveredKey(i), 100));
    for (int i = 0; i < n; ++i) events.push_back(noteOff(offFrame, coveredKey(i)));
    return events;
}

int stuckVoicesAtTenSeconds(const std::vector<MidiEvent>& events)
{
    PlaybackEngine engine;  // default pool: 96 voices
    engine.prepare(48000, 512);
    engine.setInstrument(makeDiagnosticInstrument({48000, 0.6f, 0.3f, 7}));
    std::vector<float> l(512), r(512);
    engine.process(nullptr, 0, l.data(), r.data(), 512);  // adopt snapshot

    renderBlocks(engine, events, 480256, 512);  // ≥ 10 s
    return engine.activeVoiceCount();
}

} // namespace

TEST_CASE("note-off during steal fade never leaves stuck voices", "[playback][stucknotes]")
{
    // Each sustain note-on selects 2 crossfading dynamic layers, so N notes
    // demand 2N voices; N ≥ 49 overflows the 96-voice pool and steals.
    for (int n : {40, 50, 120}) {
        DYNAMIC_SECTION("N=" << n) {
            CHECK(stuckVoicesAtTenSeconds(burst(n, 100)) == 0);
        }
    }
}

TEST_CASE("same-frame on/off burst never leaves stuck voices", "[playback][stucknotes]")
{
    // Offs in the same frame as the ons: pendings are still at fade start.
    CHECK(stuckVoicesAtTenSeconds(burst(120, 0)) == 0);
}

TEST_CASE("note-off under pedal during steal fade resolves on pedal up", "[playback][stucknotes]")
{
    for (uint32_t pedalUpFrame : {120u, 400u}) {  // during fade / after pending start
        DYNAMIC_SECTION("pedal up at frame " << pedalUpFrame) {
            std::vector<MidiEvent> events{controller(0, 64, 127)};  // pedal down
            auto b = burst(120, 100);
            events.insert(events.end(), b.begin(), b.end());
            events.push_back(controller(pedalUpFrame, 64, 0));
            CHECK(stuckVoicesAtTenSeconds(events) == 0);
        }
    }
}

TEST_CASE("CC123 all-notes-off reaches pending voices", "[playback][stucknotes]")
{
    auto events = burst(120, 0);
    events.erase(std::remove_if(events.begin(), events.end(),
                                [](const MidiEvent& e) { return e.type == MidiEvent::Type::NoteOff; }),
                 events.end());
    events.push_back(controller(100, 123, 0));  // soft all-notes-off inside the fade window
    CHECK(stuckVoicesAtTenSeconds(events) == 0);
}

TEST_CASE("off for a previous same-key note does not kill the new note", "[playback][stucknotes]")
{
    // 2-voice pool. Voice A holds key 60; on(69) steals A (pending 69);
    // off(69) inside the fade window must end THAT instance only; a second
    // on(69) steals B and must survive and keep sounding.
    EngineConfig config;
    config.maxVoices = 2;
    PlaybackEngine engine(config);
    engine.prepare(48000, 512);
    engine.setInstrument(makeSineInstrument(440.0, 48000, 69, 4.0));
    std::vector<float> l(512), r(512);
    engine.process(nullptr, 0, l.data(), r.data(), 512);

    std::vector<MidiEvent> events{
        noteOn(0, 60, 100), noteOn(0, 64, 100),  // fill the pool
        noteOn(10, 69, 100),                     // steals oldest → pending 69
        noteOff(40, 69),                         // off inside the fade window
        noteOn(60, 69, 100),                     // new instance, steals the other voice
    };
    auto out = renderBlocks(engine, events, 24000, 512);  // 0.5 s
    CHECK(engine.activeVoiceCount() == 1);  // only the surviving new instance

    // It is genuinely sounding (not a silent zombie)...
    float latePeak = 0.0f;
    for (size_t i = 20000; i < out.left.size(); ++i)
        latePeak = std::max(latePeak, std::abs(out.left[i]));
    CHECK(latePeak > 0.05f);

    // ...and its own note-off still releases it.
    renderBlocks(engine, {noteOff(0, 69)}, 24000, 512);
    CHECK(engine.activeVoiceCount() == 0);
}
