#pragma once
// SFZ text → InstrumentDefinition. Runs off the audio thread only.
// Handles: // comments, #include (depth-limited, cycle-safe), #define,
// <control>/<global>/<master>/<group>/<region> inheritance, values with
// spaces, quoted paths, mixed path separators, safe numeric conversion.
// Unsupported opcodes are recorded, never fatal.

#include <filesystem>
#include <string>
#include <vector>

#include "InstrumentDefinition.h"
#include "Types.h"

namespace sapp::sounds {

struct SfzParseResult {
    InstrumentDefinition instrument;
    std::vector<Diagnostic> diagnostics;
    bool ok = false;  // false only for unreadable/empty/limit-violating input

    bool hasErrors() const noexcept
    {
        for (const auto& d : diagnostics)
            if (d.severity == Severity::Error) return true;
        return false;
    }
};

struct SfzParserLimits {
    int maxIncludeDepth = 8;
    size_t maxRegions = 65536;
    size_t maxFileBytes = 32u * 1024u * 1024u;
    size_t maxTokenLength = 4096;
};

class SfzParser {
public:
    explicit SfzParser(SfzParserLimits limits = {}) : limits_(limits) {}

    // Parse an SFZ file from disk (resolves #include relative to the file).
    SfzParseResult parseFile(const std::filesystem::path& path) const;

    // Parse from a string; baseDir resolves includes and default_path.
    SfzParseResult parseString(const std::string& text,
                               const std::filesystem::path& baseDir,
                               const std::string& displayName = "inline") const;

private:
    SfzParserLimits limits_;
};

} // namespace sapp::sounds
