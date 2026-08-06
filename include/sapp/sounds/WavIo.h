#pragma once
// Minimal, dependency-free RIFF/WAVE decode + encode.
// Decode: PCM 8/16/24/32, IEEE float32/64, mono/stereo (extra channels are
// mixed down to the first two), 'smpl' loop chunk. Malformed chunks fail
// gracefully with a diagnostic, never crash.
// Off-audio-thread use only.

#include <cstdint>
#include <filesystem>
#include <string>

#include "SampleData.h"

namespace sapp::sounds {

struct WavDecodeResult {
    bool ok = false;
    std::string error;
};

// Fills `out` (rate, channels, frames, data, embeddedLoop, peak, rms).
WavDecodeResult decodeWavFile(const std::filesystem::path& path, SampleData& out);

// Encode interleaved-by-plane float data to 16-bit PCM or 32-bit float WAV.
bool writeWavFile(const std::filesystem::path& path,
                  const float* left, const float* right, uint64_t frames,
                  uint32_t sampleRate, bool asFloat32 = true);

} // namespace sapp::sounds
