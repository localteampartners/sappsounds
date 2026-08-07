#include <catch2/catch_test_macros.hpp>

#include <fstream>

#include "TestHelpers.h"
#include "sapp/sounds/InstrumentLoader.h"
#include "sapp/sounds/WavIo.h"

using namespace sapp::sounds;
using namespace sapptest;

namespace {

// Build a tiny on-disk library: two WAVs + an SFZ referencing them.
std::filesystem::path makeLibrary()
{
    const auto dir = tempDir() / "lib";
    std::filesystem::create_directories(dir / "Samples");

    auto a = makeSine(261.63, 48000, 0.2);
    auto c = makeSine(523.25, 48000, 0.2);
    writeWavFile(dir / "Samples" / "c4.wav", a.data[0].data(), nullptr, a.frames, 48000, true);
    writeWavFile(dir / "Samples" / "c5.wav", c.data[0].data(), nullptr, c.frames, 48000, true);

    std::ofstream sfz(dir / "instrument.sfz");
    sfz << "<control> default_path=Samples/\n"
        << "<region> sample=c4.wav lokey=48 hikey=65 pitch_keycenter=60\n"
        << "<region> sample=c5.wav lokey=66 hikey=84 pitch_keycenter=72\n"
        << "<region> sample=missing.wav lokey=85 hikey=100 pitch_keycenter=90\n";
    return dir / "instrument.sfz";
}

} // namespace

TEST_CASE("loader resolves default_path and decodes samples", "[loader]")
{
    InstrumentLoader loader;
    auto result = loader.loadSfz(makeLibrary());
    REQUIRE(result.ok);
    REQUIRE(result.instrument != nullptr);
    CHECK(result.instrument->definition.regions.size() == 3);
    CHECK(result.instrument->samples.size() == 3);

    // Two decoded, one missing.
    CHECK(result.missingSamples.size() == 1);
    CHECK(result.missingSamples[0] == "Samples/missing.wav");  // default_path baked in

    int playable = 0;
    for (const auto& r : result.instrument->definition.regions)
        if (r.sample != kInvalidSample) ++playable;
    CHECK(playable == 2);
}

TEST_CASE("loaded instrument plays through the engine", "[loader]")
{
    InstrumentLoader loader;
    auto result = loader.loadSfz(makeLibrary());
    REQUIRE(result.ok);

    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(result.instrument);

    auto out = renderBlocks(engine, {noteOn(0, 60, 100)}, 4800);
    float peak = 0.0f;
    for (float v : out.left) peak = std::max(peak, std::abs(v));
    CHECK(peak > 0.05f);
}

TEST_CASE("missing note range is silent but does not crash", "[loader]")
{
    InstrumentLoader loader;
    auto result = loader.loadSfz(makeLibrary());
    REQUIRE(result.ok);

    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(result.instrument);

    // Note 90 maps only to the missing sample → nothing plays, no crash.
    auto out = renderBlocks(engine, {noteOn(0, 90, 100)}, 2400);
    float peak = 0.0f;
    for (float v : out.left) peak = std::max(peak, std::abs(v));
    CHECK(peak == 0.0f);
    CHECK(engine.activeVoiceCount() == 0);
}

TEST_CASE("instrument with all samples missing fails cleanly", "[loader]")
{
    const auto dir = tempDir() / "broken";
    std::filesystem::create_directories(dir);
    std::ofstream sfz(dir / "broken.sfz");
    sfz << "<region> sample=nope.wav key=60\n";
    sfz.close();

    InstrumentLoader loader;
    auto result = loader.loadSfz(dir / "broken.sfz");
    CHECK_FALSE(result.ok);
    CHECK(result.instrument == nullptr);
    CHECK(result.missingSamples.size() == 1);
}

TEST_CASE("case-insensitive sample resolution", "[loader]")
{
    const auto dir = tempDir() / "caselib";
    std::filesystem::create_directories(dir / "Samples");
    auto a = makeSine(440.0, 48000, 0.1);
    writeWavFile(dir / "Samples" / "Tone.wav", a.data[0].data(), nullptr, a.frames, 48000, true);

    std::ofstream sfz(dir / "case.sfz");
    sfz << "<region> sample=samples/tone.WAV key=60\n";
    sfz.close();

    InstrumentLoader loader;
    auto result = loader.loadSfz(dir / "case.sfz");
    // On case-insensitive filesystems (macOS default) the direct path hits;
    // on case-sensitive ones the fallback walk finds it. Either way: loaded.
    REQUIRE(result.ok);
    CHECK(result.missingSamples.empty());
}
