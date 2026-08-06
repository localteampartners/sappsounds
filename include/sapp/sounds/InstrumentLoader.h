#pragma once
// Parse an SFZ file, resolve sample paths, decode audio, and produce an
// immutable LoadedInstrument snapshot. Runs on caller/background threads;
// never on the audio thread. Decoding uses a bounded worker pool.

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "InstrumentDefinition.h"
#include "SfzParser.h"
#include "Types.h"

namespace sapp::sounds {

struct LoaderOptions {
    int decodeThreads = 4;            // bounded concurrency for sample decode
    bool failOnMissingSamples = false; // default: report + keep playable subset
    SfzParserLimits parserLimits;
};

struct LoadResult {
    InstrumentPtr instrument;          // nullptr on unrecoverable failure
    std::vector<Diagnostic> diagnostics;
    std::vector<std::string> missingSamples;  // relative paths not found
    bool ok = false;
};

class InstrumentLoader {
public:
    explicit InstrumentLoader(LoaderOptions options = {}) : options_(options) {}

    // Load from an SFZ file on disk.
    LoadResult loadSfz(const std::filesystem::path& sfzPath) const;

    // Attach decoded samples to an already-parsed definition (samples are
    // resolved relative to definition.sourcePath / defaultPath).
    LoadResult loadSamples(InstrumentDefinition definition) const;

private:
    LoadResult loadSamplesInternal(InstrumentDefinition definition,
                                   std::vector<Diagnostic> diagnostics) const;
    LoaderOptions options_;
};

} // namespace sapp::sounds
