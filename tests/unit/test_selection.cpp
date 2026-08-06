#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "sapp/sounds/PlaybackEngine.h"

using namespace sapp::sounds;
using namespace sapptest;

namespace {

// Instrument with two velocity layers, keyswitched articulations, and RR.
std::shared_ptr<LoadedInstrument> makeSelectionInstrument()
{
    auto inst = std::make_shared<LoadedInstrument>();
    inst->definition.name = "selection";

    auto addSample = [&](double freq) {
        inst->samples.push_back(makeSine(freq, 48000, 0.5));
        return SampleIndex(inst->samples.size() - 1);
    };

    auto addRegion = [&](SampleIndex sample, uint8_t loVel, uint8_t hiVel, int swLast,
                         uint16_t seqLen, uint16_t seqPos) {
        RegionDefinition r;
        r.sample = sample;
        r.samplePath = "gen";
        r.loKey = 40; r.hiKey = 80; r.rootKey = 60;
        r.loVel = loVel; r.hiVel = hiVel;
        r.swLoKey = 24; r.swHiKey = 25;
        r.swLast = swLast;
        r.seqLength = seqLen; r.seqPosition = seqPos;
        r.ampeg.release = 0.01f;
        inst->definition.regions.push_back(r);
        return inst->definition.regions.size() - 1;
    };

    // Articulation A (ks 24): soft layer + loud layer.
    addRegion(addSample(220), 0, 63, 24, 1, 1);
    addRegion(addSample(440), 64, 127, 24, 1, 1);
    // Articulation B (ks 25): 3 round robins.
    addRegion(addSample(300), 0, 127, 25, 3, 1);
    addRegion(addSample(310), 0, 127, 25, 3, 2);
    addRegion(addSample(320), 0, 127, 25, 3, 3);

    inst->definition.keyswitchLo = 24;
    inst->definition.keyswitchHi = 25;
    inst->definition.defaultKeyswitch = 24;
    return inst;
}

DiagnosticSnapshot playNote(PlaybackEngine& engine, uint8_t note, uint8_t vel,
                            std::vector<MidiEvent> pre = {})
{
    std::vector<MidiEvent> events = std::move(pre);
    events.push_back(noteOn(0, note, vel));
    renderBlocks(engine, events, 256);
    DiagnosticSnapshot snap;
    REQUIRE(engine.diagnostics().read(snap));
    return snap;
}

} // namespace

TEST_CASE("velocity layer boundaries select exactly one region", "[selection]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeSelectionInstrument());

    auto s1 = playNote(engine, 60, 63);
    CHECK(s1.lastNote.selectedCount == 1);
    CHECK(s1.lastNote.decisions[0].reason == RejectReason::None);  // soft layer

    auto s2 = playNote(engine, 60, 64);
    CHECK(s2.lastNote.selectedCount == 1);
    // The first candidate (soft layer) must be rejected on velocity.
    CHECK(s2.lastNote.decisions[0].reason == RejectReason::Velocity);
}

TEST_CASE("keyswitch changes articulation and blocks other regions", "[selection]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeSelectionInstrument());

    // Default keyswitch 24 → articulation A only.
    auto s1 = playNote(engine, 60, 100);
    CHECK(s1.lastNote.selectedCount == 1);

    // Press keyswitch 25, then play: articulation B (round robin 1 of 3).
    auto s2 = playNote(engine, 60, 100, {noteOn(0, 25, 100)});
    CHECK(s2.activeKeyswitch == 25);
    CHECK(s2.lastNote.selectedCount == 1);
}

TEST_CASE("keyswitch press itself is reported and selects nothing", "[selection]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeSelectionInstrument());
    auto s = playNote(engine, 25, 100);
    CHECK(s.lastNote.wasKeyswitch);
    CHECK(s.lastNote.selectedCount == 0);
}

TEST_CASE("round robin cycles deterministically and resets", "[selection]")
{
    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(makeSelectionInstrument());

    // Switch to articulation B.
    playNote(engine, 60, 100, {noteOn(0, 25, 100)});

    std::vector<RegionIndex> picks;
    for (int i = 0; i < 6; ++i) {
        auto s = playNote(engine, 62, 100);
        REQUIRE(s.lastNote.selectedCount == 1);
        for (int d = 0; d < s.lastNote.candidateCount; ++d)
            if (s.lastNote.decisions[d].reason == RejectReason::None)
                picks.push_back(s.lastNote.decisions[d].region);
    }
    REQUIRE(picks.size() == 6);
    // Cycle of 3, twice.
    CHECK(picks[0] == picks[3]);
    CHECK(picks[1] == picks[4]);
    CHECK(picks[2] == picks[5]);
    CHECK(picks[0] != picks[1]);
    CHECK(picks[1] != picks[2]);

    // resetSequences returns to the first position.
    engine.resetSequences();
    auto s = playNote(engine, 62, 100);
    RegionIndex first = 0;
    for (int d = 0; d < s.lastNote.candidateCount; ++d)
        if (s.lastNote.decisions[d].reason == RejectReason::None) first = s.lastNote.decisions[d].region;
    CHECK(first == picks[0]);
}

TEST_CASE("cc conditions gate regions", "[selection]")
{
    auto inst = std::make_shared<LoadedInstrument>();
    inst->samples.push_back(makeSine(440, 48000, 0.5));
    RegionDefinition r;
    r.sample = 0;
    r.samplePath = "gen";
    r.loKey = 0; r.hiKey = 127; r.rootKey = 60;
    r.ccConditions.push_back({20, 64, 127});
    inst->definition.regions.push_back(r);

    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(inst);

    auto s1 = playNote(engine, 60, 100);
    CHECK(s1.lastNote.selectedCount == 0);
    CHECK(s1.lastNote.decisions[0].reason == RejectReason::CcCondition);

    auto s2 = playNote(engine, 60, 100, {controller(0, 20, 100)});
    CHECK(s2.lastNote.selectedCount == 1);
}

TEST_CASE("release regions trigger on note-off with note-on velocity", "[selection]")
{
    auto inst = std::make_shared<LoadedInstrument>();
    inst->samples.push_back(makeSine(440, 48000, 0.5));
    inst->samples.push_back(makeSine(880, 48000, 0.2));

    RegionDefinition sustain;
    sustain.sample = 0;
    sustain.samplePath = "gen";
    sustain.loKey = 0; sustain.hiKey = 127; sustain.rootKey = 60;
    inst->definition.regions.push_back(sustain);

    RegionDefinition release;
    release.sample = 1;
    release.samplePath = "gen";
    release.loKey = 0; release.hiKey = 127; release.rootKey = 60;
    release.trigger = TriggerMode::Release;
    release.loVel = 50;  // requires the note-on to have been >= 50
    inst->definition.regions.push_back(release);

    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(inst);

    // Loud note: release sample fires → 2 voices sounding after note-off.
    std::vector<MidiEvent> events{noteOn(0, 60, 100), noteOff(100, 60)};
    renderBlocks(engine, events, 256);
    CHECK(engine.activeVoiceCount() == 2);

    // Quiet note (< 50): no release sample.
    PlaybackEngine engine2;
    engine2.prepare(48000, 512);
    engine2.setInstrument(inst);
    std::vector<MidiEvent> events2{noteOn(0, 60, 30), noteOff(100, 60)};
    renderBlocks(engine2, events2, 256);
    CHECK(engine2.activeVoiceCount() == 2 - 1);  // sustain (releasing) only
}

TEST_CASE("sustain pedal defers release and release samples", "[selection]")
{
    auto inst = std::make_shared<LoadedInstrument>();
    inst->samples.push_back(makeSine(440, 48000, 1.0));
    inst->samples.push_back(makeSine(880, 48000, 0.2));

    RegionDefinition sustain;
    sustain.sample = 0;
    sustain.samplePath = "gen";
    sustain.loKey = 0; sustain.hiKey = 127; sustain.rootKey = 60;
    sustain.ampeg.release = 0.005f;
    inst->definition.regions.push_back(sustain);

    RegionDefinition release;
    release.sample = 1;
    release.samplePath = "gen";
    release.loKey = 0; release.hiKey = 127; release.rootKey = 60;
    release.trigger = TriggerMode::Release;
    inst->definition.regions.push_back(release);

    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(inst);

    // Pedal down, note on+off: sustain keeps sounding, no release sample yet.
    std::vector<MidiEvent> events{
        controller(0, 64, 127), noteOn(10, 60, 100), noteOff(100, 60)};
    renderBlocks(engine, events, 4800);
    CHECK(engine.activeVoiceCount() == 1);

    // Pedal up: note releases and the release sample fires.
    std::vector<MidiEvent> up{controller(0, 64, 0)};
    renderBlocks(engine, up, 256);
    CHECK(engine.activeVoiceCount() == 2);
}

TEST_CASE("group off_by chokes voices", "[selection]")
{
    // Hi-hat style: group 1 = closed (chokes nothing), group 2 = open,
    // open is choked by closed via off_by.
    auto inst = std::make_shared<LoadedInstrument>();
    inst->samples.push_back(makeSine(300, 48000, 2.0));
    inst->samples.push_back(makeSine(500, 48000, 2.0));

    RegionDefinition open;
    open.sample = 0;
    open.samplePath = "gen";
    open.loKey = 40; open.hiKey = 40; open.rootKey = 40;
    open.group = 2; open.offBy = 1;
    open.loop.mode = LoopMode::OneShot;
    open.loop.explicitMode = true;
    inst->definition.regions.push_back(open);

    RegionDefinition closed;
    closed.sample = 1;
    closed.samplePath = "gen";
    closed.loKey = 42; closed.hiKey = 42; closed.rootKey = 42;
    closed.group = 1;
    closed.loop.mode = LoopMode::OneShot;
    closed.loop.explicitMode = true;
    inst->definition.regions.push_back(closed);

    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(inst);

    // Open hat rings...
    renderBlocks(engine, {noteOn(0, 40, 100)}, 512);
    CHECK(engine.activeVoiceCount() == 1);
    // ...closed hat chokes it (steal fade finishes within the block).
    renderBlocks(engine, {noteOn(0, 42, 100)}, 2048);
    CHECK(engine.activeVoiceCount() == 1);
}

TEST_CASE("first and legato trigger modes", "[selection]")
{
    auto inst = std::make_shared<LoadedInstrument>();
    inst->samples.push_back(makeSine(440, 48000, 1.0));
    inst->samples.push_back(makeSine(660, 48000, 1.0));

    RegionDefinition first;
    first.sample = 0;
    first.samplePath = "gen";
    first.loKey = 0; first.hiKey = 127; first.rootKey = 60;
    first.trigger = TriggerMode::First;
    inst->definition.regions.push_back(first);

    RegionDefinition legato;
    legato.sample = 1;
    legato.samplePath = "gen";
    legato.loKey = 0; legato.hiKey = 127; legato.rootKey = 60;
    legato.trigger = TriggerMode::Legato;
    inst->definition.regions.push_back(legato);

    PlaybackEngine engine;
    engine.prepare(48000, 512);
    engine.setInstrument(inst);

    auto s1 = playNote(engine, 60, 100);
    REQUIRE(s1.lastNote.selectedCount == 1);
    CHECK(s1.lastNote.decisions[0].reason == RejectReason::None);       // first fires
    CHECK(s1.lastNote.decisions[1].reason == RejectReason::TriggerMode);

    // Second overlapping note → legato region.
    auto s2 = playNote(engine, 64, 100);
    REQUIRE(s2.lastNote.selectedCount == 1);
    CHECK(s2.lastNote.decisions[0].reason == RejectReason::TriggerMode);
    CHECK(s2.lastNote.decisions[1].reason == RejectReason::None);
}

TEST_CASE("random regions partition deterministically by seed", "[selection]")
{
    auto inst = std::make_shared<LoadedInstrument>();
    inst->samples.push_back(makeSine(440, 48000, 0.3));
    inst->samples.push_back(makeSine(550, 48000, 0.3));
    for (int i = 0; i < 2; ++i) {
        RegionDefinition r;
        r.sample = SampleIndex(i);
        r.samplePath = "gen";
        r.loKey = 0; r.hiKey = 127; r.rootKey = 60;
        r.loRand = i == 0 ? 0.0f : 0.5f;
        r.hiRand = i == 0 ? 0.5f : 1.0f;
        inst->definition.regions.push_back(r);
    }

    auto sequenceFor = [&](uint32_t seed) {
        PlaybackEngine engine;
        engine.prepare(48000, 512);
        engine.setInstrument(inst);
        engine.reseed(seed);
        std::vector<RegionIndex> picks;
        for (int i = 0; i < 8; ++i) {
            auto s = playNote(engine, 60, 100);
            for (int d = 0; d < s.lastNote.candidateCount; ++d)
                if (s.lastNote.decisions[d].reason == RejectReason::None)
                    picks.push_back(s.lastNote.decisions[d].region);
        }
        return picks;
    };

    auto a = sequenceFor(1234);
    auto b = sequenceFor(1234);
    auto c = sequenceFor(99);
    CHECK(a == b);       // deterministic per seed
    CHECK(a.size() == 8);
    // Both regions should appear over 8 rolls with overwhelming probability.
    bool sawBoth = false;
    for (size_t i = 1; i < a.size(); ++i)
        if (a[i] != a[0]) sawBoth = true;
    CHECK(sawBoth);
    (void)c;
}
