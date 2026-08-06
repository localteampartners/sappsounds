// Dynamic-layer crossfading (xfin/xfout) and legato level 2.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "TestHelpers.h"
#include "sapp/sounds/SfzParser.h"

using namespace sapp::sounds;
using namespace sapptest;
using Catch::Approx;

namespace {

// Two full-range layers crossfading on CC1: soft = 300 Hz, loud = 600 Hz.
std::shared_ptr<LoadedInstrument> makeXfadeInstrument()
{
    auto inst = std::make_shared<LoadedInstrument>();
    inst->samples.push_back(makeSine(300.0, 48000, 1.0));
    inst->samples.push_back(makeSine(600.0, 48000, 1.0));
    for (int layer = 0; layer < 2; ++layer) {
        RegionDefinition r;
        r.sample = SampleIndex(layer);
        r.samplePath = "gen";
        r.loKey = 0; r.hiKey = 127; r.rootKey = 69;
        r.ampeg.release = 0.01f;
        // Loop on whole periods of both 300 Hz (160) and 600 Hz (80) @48k.
        r.loop.mode = LoopMode::Continuous;
        r.loop.explicitMode = true;
        r.loop.start = 1600;
        r.loop.end = 46399;
        RegionDefinition::CcCrossfade cf;
        cf.cc = 1;
        if (layer == 0) { cf.outLo = 20; cf.outHi = 110; }
        else            { cf.inLo = 20; cf.inHi = 110; }
        r.ccCrossfades.push_back(cf);
        inst->definition.regions.push_back(r);
    }
    return inst;
}

} // namespace

TEST_CASE("xfin/xfout opcodes parse", "[xfade]")
{
    SfzParser parser;
    auto r = parser.parseString(
        "<region> sample=a.wav key=60 xfin_locc1=20 xfin_hicc1=110 xf_cccurve=gain "
        "xfout_lovel=64 xfout_hivel=127 xfin_lokey=40 xfin_hikey=60",
        tempDir(), "xf.sfz");
    REQUIRE(r.instrument.regions.size() == 1);
    const auto& region = r.instrument.regions[0];
    REQUIRE(region.ccCrossfades.size() == 1);
    CHECK(region.ccCrossfades[0].cc == 1);
    CHECK(region.ccCrossfades[0].inLo == 20);
    CHECK(region.ccCrossfades[0].inHi == 110);
    CHECK(region.xfCcCurve == 1);  // gain
    CHECK(region.xfoutLoVel == 64);
    CHECK(region.xfoutHiVel == 127);
    CHECK(region.xfinLoKey == 40);
    CHECK(region.xfinHiKey == 60);
    CHECK(r.instrument.unsupportedOpcodeHits == 0);
}

TEST_CASE("CC1 crossfade morphs between layers while the note sounds", "[xfade]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeXfadeInstrument());

    // Note starts at CC1 = 0 (soft layer only) then CC1 sweeps to 127.
    std::vector<MidiEvent> events{controller(0, 1, 0), noteOn(10, 69, 100)};
    for (int i = 0; i <= 16; ++i)
        events.push_back(controller(uint32_t(24000 + i * 500), 1, uint8_t(i * 127 / 16)));
    auto out = renderBlocks(engine, events, 72000);

    // Both layer voices stay alive the whole time.
    CHECK(engine.activeVoiceCount() == 2);

    // First region: 300 Hz dominant. Last region: 600 Hz dominant.
    const double early = estimateFrequency(out.left, 48000, 4000, 20000);
    const double late = estimateFrequency(out.left, 48000, 52000, 70000);
    CHECK(early == Approx(300.0).margin(15.0));
    CHECK(late == Approx(600.0).margin(25.0));

    // The morph is smooth — no discontinuities from the crossfade.
    float maxJump = 0.0f;
    for (size_t i = 24000; i + 1 < out.left.size(); ++i)
        maxJump = std::max(maxJump, std::abs(out.left[i + 1] - out.left[i]));
    CHECK(maxJump < 0.1f);
}

TEST_CASE("velocity crossfade sets static layer balance", "[xfade]")
{
    auto inst = std::make_shared<LoadedInstrument>();
    inst->samples.push_back(makeSine(300.0, 48000, 0.5));
    inst->samples.push_back(makeSine(600.0, 48000, 0.5));
    for (int layer = 0; layer < 2; ++layer) {
        RegionDefinition r;
        r.sample = SampleIndex(layer);
        r.samplePath = "gen";
        r.loKey = 0; r.hiKey = 127; r.rootKey = 69;
        r.ampVeltrack = 0.0f;  // isolate the crossfade from the velocity curve
        if (layer == 0) { r.xfoutLoVel = 1; r.xfoutHiVel = 127; }
        else            { r.xfinLoVel = 1; r.xfinHiVel = 127; }
        inst->definition.regions.push_back(r);
    }

    auto freqAtVelocity = [&](uint8_t vel) {
        PlaybackEngine engine;
        engine.prepare(48000, 512);
        engine.setInstrument(inst);
        auto out = renderBlocks(engine, {noteOn(0, 69, vel)}, 12000);
        return estimateFrequency(out.left, 48000, 2000, 11000);
    };
    CHECK(freqAtVelocity(1) == Approx(300.0).margin(15.0));
    CHECK(freqAtVelocity(127) == Approx(600.0).margin(25.0));
}

TEST_CASE("legato transition fades previous note and suppresses attack", "[legato]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeSineInstrument(440.0, 48000, 69, 2.0));
    engine.setLegato(true, 0.08f, 0.04f);

    // Overlapping line: C held, E overlaps, then C released (classic slur).
    std::vector<MidiEvent> events{
        noteOn(0, 60, 100),
        noteOn(24000, 64, 100),     // 0.5 s later — legato transition
        noteOff(26400, 60),         // release old note 50 ms after
    };
    auto out = renderBlocks(engine, events, 72000);

    // After the fade completes only the new note's voice remains.
    CHECK(engine.activeVoiceCount() == 1);

    // Transition is click-free.
    float maxJump = 0.0f;
    for (size_t i = 23000; i < 30000; ++i)
        maxJump = std::max(maxJump, std::abs(out.left[i + 1] - out.left[i]));
    CHECK(maxJump < 0.15f);
}

TEST_CASE("chords never trigger legato stealing", "[legato]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeSineInstrument(440.0, 48000, 69, 2.0));
    engine.setLegato(true, 0.08f, 0.04f);

    // Three near-simultaneous notes (same block, spread over 200 samples).
    std::vector<MidiEvent> events{
        noteOn(0, 60, 100), noteOn(100, 64, 100), noteOn(200, 67, 100)};
    renderBlocks(engine, events, 24000);
    CHECK(engine.activeVoiceCount() == 3);  // full chord sounds
}

TEST_CASE("legato disabled keeps overlapping notes independent", "[legato]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeSineInstrument(440.0, 48000, 69, 2.0));
    engine.setLegato(false);

    std::vector<MidiEvent> events{noteOn(0, 60, 100), noteOn(24000, 64, 100)};
    renderBlocks(engine, events, 48000);
    CHECK(engine.activeVoiceCount() == 2);
}
