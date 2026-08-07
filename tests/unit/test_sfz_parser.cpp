#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <fstream>

#include "TestHelpers.h"
#include "sapp/sounds/SfzParser.h"

using namespace sapp::sounds;
using Catch::Approx;

static SfzParseResult parse(const std::string& text)
{
    SfzParser parser;
    return parser.parseString(text, sapptest::tempDir(), "test.sfz");
}

TEST_CASE("basic region with key mapping", "[sfz]")
{
    auto r = parse("<region> sample=a.wav key=60 lovel=10 hivel=100 volume=-3 pan=25 tune=13");
    REQUIRE(r.ok);
    REQUIRE(r.instrument.regions.size() == 1);
    const auto& region = r.instrument.regions[0];
    CHECK(region.samplePath == "a.wav");
    CHECK(region.loKey == 60);
    CHECK(region.hiKey == 60);
    CHECK(region.rootKey == 60);
    CHECK(region.loVel == 10);
    CHECK(region.hiVel == 100);
    CHECK(region.volumeDb == Approx(-3.0f));
    CHECK(region.pan == Approx(25.0f));
    CHECK(region.tuneCents == Approx(13.0f));
}

TEST_CASE("note names parse like sforzando", "[sfz]")
{
    auto r = parse("<region> sample=a.wav lokey=c4 hikey=c#4 pitch_keycenter=db4");
    REQUIRE(r.instrument.regions.size() == 1);
    CHECK(r.instrument.regions[0].loKey == 60);
    CHECK(r.instrument.regions[0].hiKey == 61);
    CHECK(r.instrument.regions[0].rootKey == 61);
}

TEST_CASE("group and global inheritance with override", "[sfz]")
{
    auto r = parse(R"(
<global> volume=-6 ampeg_release=1.5
<group> lovel=64 pan=-50
<region> sample=one.wav key=50
<region> sample=two.wav key=52 pan=10
<group> hivel=63
<region> sample=three.wav key=54
)");
    REQUIRE(r.instrument.regions.size() == 3);
    // Region 1 inherits group+global.
    CHECK(r.instrument.regions[0].volumeDb == Approx(-6.0f));
    CHECK(r.instrument.regions[0].loVel == 64);
    CHECK(r.instrument.regions[0].pan == Approx(-50.0f));
    CHECK(r.instrument.regions[0].ampeg.release == Approx(1.5f));
    // Region 2 overrides pan.
    CHECK(r.instrument.regions[1].pan == Approx(10.0f));
    // New group resets: region 3 has default lovel, new hivel.
    CHECK(r.instrument.regions[2].loVel == 0);
    CHECK(r.instrument.regions[2].hiVel == 63);
    CHECK(r.instrument.regions[2].volumeDb == Approx(-6.0f));  // global persists
}

TEST_CASE("sample paths with spaces and backslashes", "[sfz]")
{
    auto r = parse("<region> sample=Samples\\Violin Sustain C4 rr1.wav key=60 volume=-2");
    REQUIRE(r.instrument.regions.size() == 1);
    CHECK(r.instrument.regions[0].samplePath == "Samples/Violin Sustain C4 rr1.wav");
    CHECK(r.instrument.regions[0].volumeDb == Approx(-2.0f));
}

TEST_CASE("comments and blank lines are ignored", "[sfz]")
{
    auto r = parse(R"(
// a header comment
<region> sample=a.wav key=60 // trailing comment
)");
    REQUIRE(r.instrument.regions.size() == 1);
}

TEST_CASE("define substitution", "[sfz]")
{
    auto r = parse(R"(
#define $KEY 62
#define $DIR Strings
<region> sample=$DIR/a.wav key=$KEY
)");
    REQUIRE(r.instrument.regions.size() == 1);
    CHECK(r.instrument.regions[0].samplePath == "Strings/a.wav");
    CHECK(r.instrument.regions[0].loKey == 62);
}

TEST_CASE("include files and recursion guard", "[sfz]")
{
    namespace fs = std::filesystem;
    const auto dir = sapptest::tempDir();
    {
        std::ofstream inc(dir / "inc.sfz");
        inc << "<region> sample=included.wav key=40\n";
    }
    {
        std::ofstream root(dir / "root.sfz");
        root << "#include \"inc.sfz\"\n<region> sample=direct.wav key=41\n";
    }
    SfzParser parser;
    auto r = parser.parseFile(dir / "root.sfz");
    REQUIRE(r.ok);
    REQUIRE(r.instrument.regions.size() == 2);
    CHECK(r.instrument.regions[0].samplePath == "included.wav");

    // Self-including file must fail safely, not hang or crash.
    {
        std::ofstream loop(dir / "loop.sfz");
        loop << "#include \"loop.sfz\"\n";
    }
    auto r2 = parser.parseFile(dir / "loop.sfz");
    CHECK(r2.hasErrors());
}

TEST_CASE("unsupported opcodes are recorded, not fatal", "[sfz]")
{
    auto r = parse("<region> sample=a.wav key=60 fil_type=lpf_2p cutoff=800");
    REQUIRE(r.instrument.regions.size() == 1);
    CHECK(r.instrument.unsupportedOpcodes.size() == 2);
    CHECK(r.instrument.unsupportedOpcodeHits == 2);
}

TEST_CASE("invalid numeric values produce warnings and keep defaults", "[sfz]")
{
    auto r = parse("<region> sample=a.wav key=60 volume=loud");
    REQUIRE(r.instrument.regions.size() == 1);
    CHECK(r.instrument.regions[0].volumeDb == Approx(0.0f));
    bool warned = false;
    for (const auto& d : r.diagnostics)
        if (d.severity == Severity::Warning) warned = true;
    CHECK(warned);
}

TEST_CASE("region without sample is dropped with warning", "[sfz]")
{
    auto r = parse("<region> key=60\n<region> sample=ok.wav key=61");
    REQUIRE(r.instrument.regions.size() == 1);
    CHECK(r.instrument.regions[0].samplePath == "ok.wav");
}

TEST_CASE("keyswitch opcodes and articulation derivation", "[sfz]")
{
    auto r = parse(R"(
<global> sw_lokey=24 sw_hikey=26 sw_default=24
<group> sw_last=24 sw_label=Sustain
<region> sample=sus.wav key=60
<group> sw_last=25 sw_label=Staccato
<region> sample=stac1.wav key=60 seq_length=2 seq_position=1
<region> sample=stac2.wav key=60 seq_length=2 seq_position=2
)");
    REQUIRE(r.instrument.regions.size() == 3);
    REQUIRE(r.instrument.articulations.size() == 2);
    CHECK(r.instrument.articulations[0].name == "Sustain");
    CHECK(r.instrument.articulations[0].keyswitch == 24);
    CHECK(r.instrument.articulations[0].isDefault);
    CHECK(r.instrument.articulations[1].name == "Staccato");
    CHECK(r.instrument.articulations[1].regionCount == 2);
    CHECK(r.instrument.keyswitchLo == 24);
    CHECK(r.instrument.keyswitchHi == 26);
    CHECK(r.instrument.defaultKeyswitch == 24);
}

TEST_CASE("cc conditions parse into ranges", "[sfz]")
{
    auto r = parse("<region> sample=a.wav key=60 locc64=64 hicc64=127 locc1=0 hicc1=63");
    REQUIRE(r.instrument.regions.size() == 1);
    REQUIRE(r.instrument.regions[0].ccConditions.size() == 2);
}

TEST_CASE("control header default_path and cc labels", "[sfz]")
{
    auto r = parse(R"(
<control> default_path=Samples\ set_cc1=64 label_cc1=Dynamics
<region> sample=a.wav key=60
)");
    CHECK(r.instrument.defaultPath == "Samples/");
    REQUIRE(r.instrument.controlDefaults.size() == 1);
    CHECK(r.instrument.controlDefaults[0].cc == 1);
    CHECK(r.instrument.controlDefaults[0].value == 64);
    REQUIRE(r.instrument.controlLabels.size() == 1);
    CHECK(r.instrument.controlLabels[0].label == "Dynamics");
}

TEST_CASE("loop opcodes", "[sfz]")
{
    auto r = parse("<region> sample=a.wav key=60 loop_mode=loop_sustain loop_start=100 loop_end=4000 loop_crossfade=0.05");
    REQUIRE(r.instrument.regions.size() == 1);
    const auto& region = r.instrument.regions[0];
    CHECK(region.loop.mode == LoopMode::Sustain);
    CHECK(region.loop.explicitMode);
    CHECK(region.loop.start == 100);
    CHECK(region.loop.end == 4000);
    CHECK(region.loop.crossfadeSeconds == Approx(0.05f));
}

TEST_CASE("source line numbers survive to regions", "[sfz]")
{
    auto r = parse("\n\n<region> sample=a.wav key=60");
    REQUIRE(r.instrument.regions.size() == 1);
    CHECK(r.instrument.regions[0].sourceLine == 3);
}

TEST_CASE("default_path is positional per control header", "[sfz]")
{
    // VSCO2-CE style: each articulation block re-points default_path.
    auto r = parse(R"(
<control> default_path=Strings\Violin\susVib\
<region> sample=vln_sus_C4.wav key=60
<control> default_path=Strings\Violin\Pizz\
<region> sample=vln_pizz_C4.wav key=60
)");
    REQUIRE(r.instrument.regions.size() == 2);
    CHECK(r.instrument.regions[0].samplePath == "Strings/Violin/susVib/vln_sus_C4.wav");
    CHECK(r.instrument.regions[1].samplePath == "Strings/Violin/Pizz/vln_pizz_C4.wav");
}
