#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "TestHelpers.h"
#include "sapp/sounds/DiagnosticInstrument.h"
#include "sapp/sounds/OfflineRender.h"

using namespace sapp::sounds;
using namespace sapptest;
using Catch::Approx;

TEST_CASE("root key plays at source frequency", "[playback]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeSineInstrument(440.0, 48000, 69));

    auto out = renderBlocks(engine, {noteOn(0, 69, 127)}, 24000);
    const double freq = estimateFrequency(out.left, 48000, 2000, 22000);
    CHECK(freq == Approx(440.0).margin(6.0));
}

TEST_CASE("transposition one octave doubles frequency", "[playback]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeSineInstrument(440.0, 48000, 69));

    auto out = renderBlocks(engine, {noteOn(0, 81, 127)}, 24000);
    const double freq = estimateFrequency(out.left, 48000, 2000, 22000);
    CHECK(freq == Approx(880.0).margin(10.0));
}

TEST_CASE("sample rate conversion is pitch-correct", "[playback]")
{
    // 44.1k source rendered at 48k must still sound at 440 Hz.
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeSineInstrument(440.0, 44100, 69));

    auto out = renderBlocks(engine, {noteOn(0, 69, 127)}, 24000);
    const double freq = estimateFrequency(out.left, 48000, 2000, 22000);
    CHECK(freq == Approx(440.0).margin(6.0));
}

TEST_CASE("release envelope decays to silence", "[playback]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeSineInstrument(440.0, 48000, 69, 2.0));

    // Note off at frame 4800 (0.1 s); release is 0.02 s → silent well before 1 s.
    auto out = renderBlocks(engine, {noteOn(0, 69, 127), noteOff(4800, 69)}, 48000);
    float tailPeak = 0.0f;
    for (size_t i = 40000; i < out.left.size(); ++i)
        tailPeak = std::max(tailPeak, std::abs(out.left[i]));
    CHECK(tailPeak < 1.0e-3f);
    CHECK(engine.activeVoiceCount() == 0);
}

TEST_CASE("looped sustain plays past the sample end without clicks", "[playback]")
{
    auto inst = std::make_shared<LoadedInstrument>();
    inst->samples.push_back(makeSine(200.0, 48000, 0.5));  // exact period fits: 200Hz@48k = 240 frames
    RegionDefinition r;
    r.sample = 0;
    r.samplePath = "gen";
    r.rootKey = 69; r.loKey = 0; r.hiKey = 127;
    r.loop.mode = LoopMode::Continuous;
    r.loop.explicitMode = true;
    r.loop.start = 4800;
    r.loop.end = 4800 + 240 * 50 - 1;  // whole periods → seamless
    inst->definition.regions.push_back(r);

    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(inst);

    // Hold for 2 s — four times the sample length.
    auto out = renderBlocks(engine, {noteOn(0, 69, 127)}, 96000);
    CHECK(engine.activeVoiceCount() == 1);

    // Still producing signal at the end (looping), and no huge discontinuity.
    float latePeak = 0.0f;
    float maxJump = 0.0f;
    for (size_t i = 90000; i < out.left.size(); ++i)
        latePeak = std::max(latePeak, std::abs(out.left[i]));
    for (size_t i = 10000; i + 1 < out.left.size(); ++i)
        maxJump = std::max(maxJump, std::abs(out.left[i + 1] - out.left[i]));
    CHECK(latePeak > 0.1f);
    CHECK(maxJump < 0.1f);  // a click would jump near full scale
}

TEST_CASE("stereo samples keep channel identity", "[playback]")
{
    auto inst = std::make_shared<LoadedInstrument>();
    SampleData s = makeSine(440.0, 48000, 0.5, 2);
    for (auto& v : s.data[1]) v = 0.0f;  // right channel silent
    inst->samples.push_back(std::move(s));
    RegionDefinition r;
    r.sample = 0;
    r.samplePath = "gen";
    r.rootKey = 69; r.loKey = 0; r.hiKey = 127;
    inst->definition.regions.push_back(r);

    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(inst);
    auto out = renderBlocks(engine, {noteOn(0, 69, 127)}, 12000);

    float peakL = 0.0f, peakR = 0.0f;
    for (size_t i = 0; i < out.left.size(); ++i) {
        peakL = std::max(peakL, std::abs(out.left[i]));
        peakR = std::max(peakR, std::abs(out.right[i]));
    }
    CHECK(peakL > 0.1f);
    CHECK(peakR < 1.0e-6f);
}

TEST_CASE("voice stealing stays within pool and de-clicks", "[playback]")
{
    EngineConfig config;
    config.maxVoices = 8;
    PlaybackEngine engine(config);
    engine.prepare(48000, 512);
    engine.setInstrument(makeSineInstrument(440.0, 48000, 69, 2.0));

    // 32 overlapping notes into an 8-voice pool.
    std::vector<MidiEvent> events;
    for (int i = 0; i < 32; ++i)
        events.push_back(noteOn(uint32_t(i * 200), uint8_t(40 + i), 100));
    auto out = renderBlocks(engine, events, 48000);

    CHECK(engine.activeVoiceCount() <= 8);
    DiagnosticSnapshot snap;
    REQUIRE(engine.diagnostics().read(snap));
    CHECK(snap.voicesStolen > 0);

    float maxJump = 0.0f;
    for (size_t i = 1; i < out.left.size(); ++i)
        maxJump = std::max(maxJump, std::abs(out.left[i] - out.left[i - 1]));
    CHECK(maxJump < 0.25f);  // fades keep discontinuities bounded
}

TEST_CASE("no NaNs or infinities under stress", "[playback]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeDiagnosticInstrument({48000, 0.8f, 0.4f, 42}));

    std::vector<MidiEvent> events;
    for (int i = 0; i < 64; ++i) {
        events.push_back(noteOn(uint32_t(i * 100), uint8_t(30 + (i * 7) % 60), uint8_t(1 + (i * 13) % 126)));
        if (i % 3 == 0) events.push_back(noteOff(uint32_t(i * 100 + 50), uint8_t(30 + ((i - 3) * 7) % 60)));
    }
    auto out = renderBlocks(engine, events, 48000);
    for (float v : out.left) REQUIRE(std::isfinite(v));
    for (float v : out.right) REQUIRE(std::isfinite(v));
}

TEST_CASE("offline render is deterministic for a fixed seed", "[playback]")
{
    auto inst = makeDiagnosticInstrument({48000, 0.8f, 0.4f, 42});
    std::vector<TimedMidiEvent> song;
    for (int i = 0; i < 8; ++i) {
        song.push_back({0.2 * i, 0x90, 0, uint8_t(48 + i * 3), 100, 0});
        song.push_back({0.2 * i + 0.5, 0x80, 0, uint8_t(48 + i * 3), 0, 0});
    }
    OfflineRenderOptions options;
    options.tailSeconds = 1.0;
    options.randomTuneCents = 4.0f;  // exercises the seeded RNG path

    auto a = renderOffline(inst, song, options);
    auto b = renderOffline(inst, song, options);
    REQUIRE(a.left.size() == b.left.size());
    for (size_t i = 0; i < a.left.size(); i += 97)
        REQUIRE(a.left[i] == b.left[i]);
    CHECK(a.peak > 0.01f);
}

TEST_CASE("diagnostic instrument keyswitches work end to end", "[playback]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    auto inst = makeDiagnosticInstrument({48000, 0.8f, 0.4f, 42});
    REQUIRE(inst->definition.articulations.size() == 3);
    engine.setInstrument(inst);

    // Default sustain articulation sounds — both crossfading dynamic layers.
    auto out1 = renderBlocks(engine, {noteOn(0, 60, 100)}, 9600);
    DiagnosticSnapshot snap;
    REQUIRE(engine.diagnostics().read(snap));
    CHECK(snap.lastNote.selectedCount == 2);
    CHECK(snap.activeKeyswitch == 12);

    // Switch to pizzicato.
    renderBlocks(engine, {noteOn(0, 14, 100), noteOn(10, 64, 100)}, 9600);
    REQUIRE(engine.diagnostics().read(snap));
    CHECK(snap.activeKeyswitch == 14);
    CHECK(snap.lastNote.selectedCount == 1);
}
