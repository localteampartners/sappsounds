#pragma once
// Decoded, immutable sample audio + metadata.
// Canonical form: 32-bit float, original channel count (1 or 2), original rate.

#include <cstdint>
#include <string>
#include <vector>

#include "Types.h"

namespace sapp::sounds {

struct LoopMetadata {
    bool hasLoop = false;
    uint32_t start = 0;   // frame index of first loop frame
    uint32_t end = 0;     // frame index of last loop frame (inclusive)
};

struct SampleData {
    std::string relativePath;   // as referenced by the instrument source
    std::string resolvedPath;   // absolute path on disk ("" for generated)

    uint32_t sampleRate = 0;
    uint32_t channels = 0;      // 1 or 2
    uint64_t frames = 0;

    // Non-interleaved. data[channel][frame]. channels entries, frames each.
    std::vector<std::vector<float>> data;

    LoopMetadata embeddedLoop;  // from WAV 'smpl' chunk, if present
    float peak = 0.0f;
    float rms = 0.0f;

    bool valid() const noexcept { return channels > 0 && frames > 0; }
    uint64_t bytes() const noexcept
    {
        return frames * channels * sizeof(float);
    }
};

} // namespace sapp::sounds
