// SappSoundsRenderTool — deterministic offline render: SFZ + MIDI → WAV.
//
//   SappSoundsRenderTool --sfz <file.sfz> --midi <file.mid> --out <file.wav>
//                        [--sr 48000] [--quality draft|normal] [--seed N]
//                        [--tail 3.0] [--gain 1.0] [--tune-spread cents]
//   SappSoundsRenderTool --diagnostic --midi <file.mid> --out <file.wav>
//
// Prints a JSON result line (peak/rms/duration) for machine consumption.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../common/Json.h"
#include "sapp/sounds/DiagnosticInstrument.h"
#include "sapp/sounds/InstrumentLoader.h"
#include "sapp/sounds/MidiFile.h"
#include "sapp/sounds/OfflineRender.h"
#include "sapp/sounds/WavIo.h"

using namespace sapp::sounds;

int main(int argc, char** argv)
{
    std::string sfzPath, midiPath, outPath;
    bool useDiagnostic = false;
    OfflineRenderOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--sfz") sfzPath = next();
        else if (arg == "--midi") midiPath = next();
        else if (arg == "--out") outPath = next();
        else if (arg == "--diagnostic") useDiagnostic = true;
        else if (arg == "--sr") options.sampleRate = std::atof(next().c_str());
        else if (arg == "--seed") options.seed = uint32_t(std::strtoul(next().c_str(), nullptr, 10));
        else if (arg == "--tail") options.tailSeconds = std::atof(next().c_str());
        else if (arg == "--gain") options.masterGain = float(std::atof(next().c_str()));
        else if (arg == "--tune-spread") options.randomTuneCents = float(std::atof(next().c_str()));
        else if (arg == "--quality") options.interpolationQuality = (next() == "draft") ? 0 : 1;
    }

    if ((sfzPath.empty() && !useDiagnostic) || midiPath.empty() || outPath.empty()) {
        std::fprintf(stderr,
                     "usage: SappSoundsRenderTool (--sfz <f.sfz> | --diagnostic) "
                     "--midi <f.mid> --out <f.wav> [--sr N] [--seed N] [--tail S] "
                     "[--gain G] [--quality draft|normal] [--tune-spread C]\n");
        return 2;
    }

    InstrumentPtr inst;
    if (useDiagnostic) {
        DiagnosticInstrumentOptions d;
        d.sampleRate = uint32_t(options.sampleRate);
        inst = makeDiagnosticInstrument(d);
    } else {
        InstrumentLoader loader;
        auto load = loader.loadSfz(sfzPath);
        if (!load.ok) {
            std::fprintf(stderr, "error: failed to load %s\n", sfzPath.c_str());
            for (const auto& d : load.diagnostics)
                std::fprintf(stderr, "  %s:%d %s\n", d.file.c_str(), d.line, d.message.c_str());
            return 2;
        }
        inst = load.instrument;
    }

    auto midi = readMidiFile(midiPath);
    if (!midi.ok) {
        std::fprintf(stderr, "error: %s: %s\n", midiPath.c_str(), midi.error.c_str());
        return 2;
    }

    auto rendered = renderOffline(inst, midi.events, options);
    if (rendered.left.empty()) {
        std::fprintf(stderr, "error: render produced no audio\n");
        return 2;
    }
    if (!writeWavFile(outPath, rendered.left.data(), rendered.right.data(),
                      rendered.left.size(), uint32_t(options.sampleRate), true)) {
        std::fprintf(stderr, "error: cannot write %s\n", outPath.c_str());
        return 2;
    }

    sapptools::JsonWriter w;
    w.beginObject();
    w.field("ok", true);
    w.field("out", outPath);
    w.field("sampleRate", options.sampleRate);
    w.field("frames", uint64_t(rendered.left.size()));
    w.field("durationSeconds", double(rendered.left.size()) / options.sampleRate);
    w.field("peak", double(rendered.peak));
    w.field("rms", double(rendered.rms));
    w.field("midiEvents", uint64_t(midi.events.size()));
    w.endObject();
    std::printf("%s\n", w.str().c_str());
    return 0;
}
