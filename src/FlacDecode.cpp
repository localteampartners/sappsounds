// FLAC decoding via dr_flac (public domain / MIT-0, vendored in third_party/).
// Kept in its own translation unit so the large single-header implementation
// compiles exactly once.

#include <algorithm>
#include <cmath>
#include <vector>

#define DR_FLAC_IMPLEMENTATION
#define DRFLAC_MALLOC(sz) ::malloc(sz)
#include "../third_party/dr_flac.h"

#include "sapp/sounds/WavIo.h"

namespace sapp::sounds {

WavDecodeResult decodeFlacFile(const std::filesystem::path& path, SampleData& out)
{
    WavDecodeResult result;

    drflac* flac = drflac_open_file(path.string().c_str(), nullptr);
    if (flac == nullptr) {
        result.error = "not a decodable FLAC file";
        return result;
    }

    const uint32_t channels = flac->channels;
    const uint32_t sampleRate = flac->sampleRate;
    const uint64_t frames = flac->totalPCMFrameCount;

    if (channels == 0 || channels > 64 || sampleRate < 1000 || sampleRate > 384000 ||
        frames == 0 || frames > (1ull << 31)) {
        drflac_close(flac);
        result.error = "implausible FLAC stream parameters";
        return result;
    }

    std::vector<float> interleaved(size_t(frames) * channels);
    const uint64_t read = drflac_read_pcm_frames_f32(flac, frames, interleaved.data());
    drflac_close(flac);
    if (read == 0) {
        result.error = "FLAC decode produced no frames";
        return result;
    }

    const uint32_t outChannels = std::min<uint32_t>(channels, 2);
    out.sampleRate = sampleRate;
    out.channels = outChannels;
    out.frames = read;
    out.data.assign(outChannels, std::vector<float>(size_t(read), 0.0f));

    for (uint64_t i = 0; i < read; ++i)
        for (uint32_t c = 0; c < outChannels; ++c) {
            float v = interleaved[size_t(i) * channels + c];
            if (!std::isfinite(v)) v = 0.0f;
            out.data[c][size_t(i)] = v;
        }

    // FLAC carries no smpl loop chunk; loop points come from the SFZ.
    out.embeddedLoop = {};

    double sumSq = 0.0;
    float peak = 0.0f;
    for (uint32_t c = 0; c < outChannels; ++c)
        for (float v : out.data[c]) {
            peak = std::max(peak, std::abs(v));
            sumSq += double(v) * v;
        }
    out.peak = peak;
    out.rms = float(std::sqrt(sumSq / double(std::max<uint64_t>(1, read * outChannels))));

    result.ok = true;
    return result;
}

} // namespace sapp::sounds
