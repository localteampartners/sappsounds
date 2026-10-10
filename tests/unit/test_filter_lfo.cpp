// E7 (MUSIC-QUALITY-PLAN): SFZ filter, filter envelope and LFO opcodes —
// a library can now darken soft notes and add vibrato from its own text.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include "TestHelpers.h"
#include "sapp/sounds/SfzParser.h"

using namespace sapp::sounds;
using namespace sapptest;
using Catch::Approx;

namespace {

// A bright sample: 24 harmonics of 220 Hz at 1/n — plenty above any cutoff.
std::shared_ptr<LoadedInstrument> brightInstrument(double seconds = 1.0)
{
    auto inst = std::make_shared<LoadedInstrument>();
    inst->definition.name = "bright";
    SampleData s;
    s.sampleRate = 48000;
    s.channels = 1;
    s.frames = uint64_t(48000 * seconds);
    s.data.assign(1, std::vector<float>(size_t(s.frames), 0.0f));
    for (uint64_t i = 0; i < s.frames; ++i) {
        double v = 0.0;
        for (int h = 1; h <= 24; ++h) v += std::sin(2.0 * 3.14159265358979 * 220.0 * h * double(i) / 48000.0) / h;
        s.data[0][size_t(i)] = float(v * 0.25);
    }
    s.peak = 0.9f;
    inst->samples.push_back(s);
    RegionDefinition r;
    r.sample = 0;
    r.samplePath = "bright.gen";
    r.rootKey = 57;
    r.loKey = 0;
    r.hiKey = 127;
    r.ampeg.release = 0.02f;
    inst->definition.regions.push_back(r);
    return inst;
}

double rmsOf(const std::vector<float>& x, size_t from, size_t count)
{
    double acc = 0.0;
    for (size_t i = from; i < from + count && i < x.size(); ++i) acc += double(x[i]) * x[i];
    return std::sqrt(acc / double(count));
}

// Energy above ~1.5 kHz: four cascaded one-pole high-passes (24 dB/oct, so
// the 220 Hz fundamental cannot leak into the measurement), then RMS.
double highEnergy(const std::vector<float>& x, size_t from, size_t count)
{
    const float coef = 1.0f - std::exp(-2.0f * 3.14159265f * 1500.0f / 48000.0f);
    float lp[4] { 0.0f, 0.0f, 0.0f, 0.0f };
    double acc = 0.0;
    for (size_t i = from; i < from + count && i < x.size(); ++i) {
        float v = x[i];
        for (float& state : lp) { state += coef * (v - state); v -= state; }
        acc += double(v) * v;
    }
    return std::sqrt(acc / double(count));
}

std::vector<float> renderWith(std::shared_ptr<LoadedInstrument> inst, uint8_t vel = 127, uint32_t frames = 24000)
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(inst);
    return renderBlocks(engine, {noteOn(0, 57, vel)}, frames).left;
}

} // namespace

TEST_CASE("filter and LFO opcodes parse", "[sfz][e7]")
{
    SfzParser parser;
    auto r = parser.parseString(
        "<region> sample=a.wav key=60 fil_type=lpf_2p cutoff=800 resonance=6 fil_veltrack=2400 fil_keytrack=100 "
        "fileg_attack=0.01 fileg_decay=0.3 fileg_sustain=20 fileg_depth=1200 "
        "pitchlfo_freq=5 pitchlfo_depth=30 amplfo_freq=4 amplfo_depth=3 fillfo_freq=0.5 fillfo_depth=600 pitchlfo_delay=0.2 pitchlfo_fade=0.4",
        sapptest::tempDir(), "test.sfz");
    REQUIRE(r.ok);
    const auto& g = r.instrument.regions[0];
    CHECK(g.filType == RegionDefinition::FilterType::Lpf2p);
    CHECK(g.cutoffHz == Approx(800.0f));
    CHECK(g.resonanceDb == Approx(6.0f));
    CHECK(g.filVeltrack == Approx(2400.0f));
    CHECK(g.filKeytrack == Approx(100.0f));
    CHECK(g.fileg.attack == Approx(0.01f));
    CHECK(g.fileg.sustain == Approx(0.2f));
    CHECK(g.filegDepthCents == Approx(1200.0f));
    CHECK(g.pitchlfoFreq == Approx(5.0f));
    CHECK(g.pitchlfoDepthCents == Approx(30.0f));
    CHECK(g.amplfoDepthDb == Approx(3.0f));
    CHECK(g.fillfoDepthCents == Approx(600.0f));
    CHECK(g.lfoDelay == Approx(0.2f));
    CHECK(g.lfoFade == Approx(0.4f));
}

TEST_CASE("a low-pass cutoff takes the top off a bright sample", "[playback][e7]")
{
    auto plain = brightInstrument();
    auto dark = brightInstrument();
    dark->definition.regions[0].filType = RegionDefinition::FilterType::Lpf2p;
    dark->definition.regions[0].cutoffHz = 400.0f;
    const auto a = renderWith(plain), b = renderWith(dark);
    const double hiPlain = highEnergy(a, 4000, 16000), hiDark = highEnergy(b, 4000, 16000);
    INFO("high-band energy plain " << hiPlain << " dark " << hiDark);
    CHECK(hiDark < hiPlain * 0.2);
    CHECK(rmsOf(b, 4000, 16000) > 0.01);   // still sounds
}

TEST_CASE("fil_veltrack: harder notes are brighter", "[playback][e7]")
{
    auto inst = brightInstrument();
    auto& r = inst->definition.regions[0];
    r.filType = RegionDefinition::FilterType::Lpf2p;
    r.cutoffHz = 300.0f;
    r.filVeltrack = 3600.0f;     // three octaves at full velocity
    r.ampVeltrack = 0.0f;        // level flat, so only tone moves
    const auto soft = renderWith(inst, 30), hard = renderWith(inst, 127);
    const double hiSoft = highEnergy(soft, 4000, 16000), hiHard = highEnergy(hard, 4000, 16000);
    INFO("soft " << hiSoft << " hard " << hiHard);
    CHECK(hiHard > hiSoft * 3.0);
}

TEST_CASE("fileg opens the filter and then settles", "[playback][e7]")
{
    auto inst = brightInstrument(1.5);
    auto& r = inst->definition.regions[0];
    r.filType = RegionDefinition::FilterType::Lpf2p;
    r.cutoffHz = 300.0f;
    r.fileg.attack = 0.005f;
    r.fileg.decay = 0.5f;
    r.fileg.sustain = 0.0f;
    r.filegDepthCents = 4800.0f;
    const auto x = renderWith(inst, 127, 48000);
    // 10-60 ms in: the envelope is open; 750 ms+ in: it has settled shut
    const double early = highEnergy(x, 500, 2500), late = highEnergy(x, 36000, 8000);
    INFO("early " << early << " late " << late);
    CHECK(early > late * 3.0);
}

TEST_CASE("amplfo is tremolo, pitchlfo is vibrato", "[playback][e7]")
{
    auto trem = brightInstrument();
    trem->definition.regions[0].amplfoFreq = 6.0f;
    trem->definition.regions[0].amplfoDepthDb = 6.0f;
    const auto t = renderWith(trem);
    double lo = 1e9, hi = 0.0;
    for (size_t w = 4000; w + 1000 <= 24000; w += 1000) { const double v = rmsOf(t, w, 1000); lo = std::min(lo, v); hi = std::max(hi, v); }
    INFO("tremolo window rms min " << lo << " max " << hi);
    CHECK(hi > lo * 1.5);

    auto vib = brightInstrument();
    vib->definition.regions[0].pitchlfoFreq = 5.0f;
    vib->definition.regions[0].pitchlfoDepthCents = 100.0f;
    const auto v = renderWith(vib);
    double fLo = 1e9, fHi = 0.0;
    for (size_t w = 4000; w + 2400 <= 24000; w += 1200) {
        const double f = estimateFrequency(v, 48000, w, w + 2400);
        fLo = std::min(fLo, f); fHi = std::max(fHi, f);
    }
    INFO("vibrato f min " << fLo << " max " << fHi);
    CHECK(fHi - fLo > 10.0);
}
