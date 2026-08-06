// SappSoundsInstrumentInspector — full machine-readable instrument report.
// This is the primary "agent API" surface for external software (e.g. MIDI
// generators) to learn an instrument's capabilities before writing for it.
//
//   SappSoundsInstrumentInspector <file.sfz> [--regions] [--diagnostic]
//
// Always prints JSON. --regions adds the per-region dump (can be large).
// --diagnostic inspects the built-in generated instrument instead of a file.

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>

#include "../common/Json.h"
#include "sapp/sounds/DiagnosticInstrument.h"
#include "sapp/sounds/InstrumentLoader.h"

using namespace sapp::sounds;

static const char* noteName(int note)
{
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static char buf[8];
    std::snprintf(buf, sizeof(buf), "%s%d", names[note % 12], note / 12 - 1);
    return buf;
}

int main(int argc, char** argv)
{
    std::string path;
    bool dumpRegions = false, useDiagnostic = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--regions") dumpRegions = true;
        else if (arg == "--diagnostic") useDiagnostic = true;
        else path = arg;
    }
    if (path.empty() && !useDiagnostic) {
        std::fprintf(stderr,
                     "usage: SappSoundsInstrumentInspector <file.sfz> [--regions] [--diagnostic]\n");
        return 2;
    }

    InstrumentPtr inst;
    std::vector<Diagnostic> diagnostics;
    std::vector<std::string> missing;

    if (useDiagnostic) {
        inst = makeDiagnosticInstrument();
    } else {
        InstrumentLoader loader;
        auto result = loader.loadSfz(path);
        diagnostics = result.diagnostics;
        missing = result.missingSamples;
        if (!result.ok) {
            sapptools::JsonWriter w;
            w.beginObject();
            w.field("ok", false);
            w.field("file", path);
            w.key("diagnostics");
            w.beginArray();
            for (const auto& d : diagnostics) {
                w.beginObject();
                w.field("severity", d.severity == Severity::Error ? "error" : "warning");
                w.field("message", d.message);
                w.endObject();
            }
            w.endArray();
            w.endObject();
            std::printf("%s\n", w.str().c_str());
            return 2;
        }
        inst = result.instrument;
    }

    const auto& def = inst->definition;

    // Aggregate capability facts an agent needs.
    std::set<int> velocitySplits;
    uint16_t maxRoundRobins = 1;
    bool hasReleaseSamples = false, hasLoops = false;
    for (const auto& r : def.regions) {
        velocitySplits.insert(r.loVel);
        maxRoundRobins = std::max(maxRoundRobins, r.seqLength);
        if (r.trigger == TriggerMode::Release || r.trigger == TriggerMode::ReleaseKey)
            hasReleaseSamples = true;
        if (r.loop.mode == LoopMode::Continuous || r.loop.mode == LoopMode::Sustain ||
            (!r.loop.explicitMode && r.sample >= 0 &&
             inst->samples[size_t(r.sample)].embeddedLoop.hasLoop))
            hasLoops = true;
    }

    sapptools::JsonWriter w;
    w.beginObject();
    w.field("ok", true);
    w.field("name", def.name);
    w.field("source", def.sourcePath.empty() ? std::string("(generated)") : def.sourcePath);
    w.field("regions", uint64_t(def.regions.size()));
    w.field("samples", uint64_t(inst->samples.size()));
    w.field("missingSamples", uint64_t(missing.size()));
    w.field("estimatedRamBytes", inst->sampleBytes());

    w.key("keyRange");
    w.beginObject();
    w.field("low", int(def.loKeyUsed));
    w.field("high", int(def.hiKeyUsed));
    w.field("lowName", noteName(def.loKeyUsed));
    w.field("highName", noteName(def.hiKeyUsed));
    w.endObject();

    w.key("keyswitches");
    w.beginObject();
    w.field("present", def.keyswitchLo >= 0);
    if (def.keyswitchLo >= 0) {
        w.field("low", def.keyswitchLo);
        w.field("high", def.keyswitchHi);
        w.field("default", def.defaultKeyswitch);
    }
    w.endObject();

    w.key("articulations");
    w.beginArray();
    for (const auto& a : def.articulations) {
        w.beginObject();
        w.field("name", a.name);
        w.field("keyswitch", a.keyswitch);
        if (a.keyswitch >= 0) w.field("keyswitchName", noteName(a.keyswitch));
        w.field("regions", uint64_t(a.regionCount));
        w.field("default", a.isDefault);
        w.endObject();
    }
    w.endArray();

    w.key("capabilities");
    w.beginObject();
    w.field("velocityLayers", uint64_t(velocitySplits.size()));
    w.field("roundRobins", int(maxRoundRobins));
    w.field("releaseSamples", hasReleaseSamples);
    w.field("sustainLoops", hasLoops);
    w.endObject();

    w.key("controllers");
    w.beginArray();
    for (const auto& c : def.controlLabels) {
        w.beginObject();
        w.field("cc", int(c.cc));
        w.field("label", c.label);
        w.endObject();
    }
    w.endArray();

    w.key("unsupportedOpcodes");
    w.beginArray();
    for (const auto& o : def.unsupportedOpcodes) w.value(o);
    w.endArray();

    if (dumpRegions) {
        w.key("regionDetails");
        w.beginArray();
        for (const auto& r : def.regions) {
            w.beginObject();
            w.field("sample", r.samplePath);
            w.field("loKey", int(r.loKey));
            w.field("hiKey", int(r.hiKey));
            w.field("rootKey", int(r.rootKey));
            w.field("loVel", int(r.loVel));
            w.field("hiVel", int(r.hiVel));
            w.field("volumeDb", double(r.volumeDb));
            w.field("pan", double(r.pan));
            w.field("tuneCents", double(r.tuneCents));
            w.field("seqPosition", int(r.seqPosition));
            w.field("seqLength", int(r.seqLength));
            w.field("keyswitch", r.swLast);
            w.field("trigger", r.trigger == TriggerMode::Release ? "release"
                             : r.trigger == TriggerMode::ReleaseKey ? "release_key"
                             : r.trigger == TriggerMode::First ? "first"
                             : r.trigger == TriggerMode::Legato ? "legato" : "attack");
            w.field("missing", r.sample == kInvalidSample);
            w.field("sourceLine", uint64_t(r.sourceLine));
            w.endObject();
        }
        w.endArray();
    }

    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return missing.empty() ? 0 : 1;
}
