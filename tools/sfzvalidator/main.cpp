// SappSoundsSFZValidator — parse + resolve an SFZ instrument and report.
//
//   SappSoundsSFZValidator <file.sfz> [--json] [--no-samples]
//
// Exit codes: 0 valid/playable, 1 warnings only, 2 errors/unplayable.

#include <cstdio>
#include <string>

#include "../common/Json.h"
#include "sapp/sounds/InstrumentLoader.h"
#include "sapp/sounds/SfzParser.h"

using namespace sapp::sounds;

int main(int argc, char** argv)
{
    std::string path;
    bool json = false, checkSamples = true;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--json") json = true;
        else if (arg == "--no-samples") checkSamples = false;
        else path = arg;
    }
    if (path.empty()) {
        std::fprintf(stderr, "usage: SappSoundsSFZValidator <file.sfz> [--json] [--no-samples]\n");
        return 2;
    }

    std::vector<Diagnostic> diagnostics;
    size_t regionCount = 0, missingCount = 0;
    std::vector<std::string> unsupported;
    uint32_t recognized = 0, unsupportedHits = 0;
    bool playable = false;

    if (checkSamples) {
        InstrumentLoader loader;
        auto result = loader.loadSfz(path);
        diagnostics = result.diagnostics;
        missingCount = result.missingSamples.size();
        playable = result.ok;
        if (result.instrument) {
            regionCount = result.instrument->definition.regions.size();
            unsupported = result.instrument->definition.unsupportedOpcodes;
            recognized = result.instrument->definition.recognizedOpcodeHits;
            unsupportedHits = result.instrument->definition.unsupportedOpcodeHits;
        }
    } else {
        SfzParser parser;
        auto result = parser.parseFile(path);
        diagnostics = result.diagnostics;
        playable = result.ok && !result.instrument.regions.empty();
        regionCount = result.instrument.regions.size();
        unsupported = result.instrument.unsupportedOpcodes;
        recognized = result.instrument.recognizedOpcodeHits;
        unsupportedHits = result.instrument.unsupportedOpcodeHits;
    }

    int errors = 0, warnings = 0;
    for (const auto& d : diagnostics) {
        if (d.severity == Severity::Error) ++errors;
        else if (d.severity == Severity::Warning) ++warnings;
    }
    const double total = double(recognized + unsupportedHits);
    const double recognizedPct = total > 0 ? 100.0 * double(recognized) / total : 100.0;

    if (json) {
        sapptools::JsonWriter w;
        w.beginObject();
        w.field("file", path);
        w.field("playable", playable);
        w.field("regions", uint64_t(regionCount));
        w.field("missingSamples", uint64_t(missingCount));
        w.field("recognizedOpcodePercent", recognizedPct);
        w.field("errors", errors);
        w.field("warnings", warnings);
        w.key("unsupportedOpcodes");
        w.beginArray();
        for (const auto& o : unsupported) w.value(o);
        w.endArray();
        w.key("diagnostics");
        w.beginArray();
        for (const auto& d : diagnostics) {
            w.beginObject();
            w.field("severity", d.severity == Severity::Error ? "error"
                              : d.severity == Severity::Warning ? "warning" : "info");
            w.field("file", d.file);
            w.field("line", d.line);
            w.field("message", d.message);
            w.endObject();
        }
        w.endArray();
        w.endObject();
        std::printf("%s\n", w.str().c_str());
    } else {
        std::printf("Instrument: %s\n", path.c_str());
        std::printf("Regions parsed: %zu\n", regionCount);
        std::printf("Recognized opcodes: %.1f%%\n", recognizedPct);
        std::printf("Ignored opcodes: %zu\n", unsupported.size());
        std::printf("Missing samples: %zu\n", missingCount);
        std::printf("Playable: %s\n", playable ? "Yes" : "No");
        for (const auto& d : diagnostics)
            std::printf("  [%s] %s:%d %s\n",
                        d.severity == Severity::Error ? "error"
                        : d.severity == Severity::Warning ? "warn" : "info",
                        d.file.c_str(), d.line, d.message.c_str());
    }

    if (!playable || errors > 0) return 2;
    return warnings > 0 ? 1 : 0;
}
