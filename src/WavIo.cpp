#include "sapp/sounds/WavIo.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace sapp::sounds {
namespace {

constexpr uint64_t kMaxWavBytes = 2ull * 1024 * 1024 * 1024;  // 2 GiB safety cap
constexpr uint64_t kMaxFrames = 1ull << 31;

uint32_t readU32(const uint8_t* p) noexcept
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t readU16(const uint8_t* p) noexcept
{
    return uint16_t(uint32_t(p[0]) | (uint32_t(p[1]) << 8));
}

float pcm24ToFloat(const uint8_t* p) noexcept
{
    int32_t v = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
    if (v & 0x800000) v |= ~0xFFFFFF;  // sign extend
    return float(v) / 8388608.0f;
}

} // namespace

WavDecodeResult decodeAudioFile(const std::filesystem::path& path, SampleData& out)
{
    std::ifstream probe(path, std::ios::binary);
    char magic[4] = {};
    probe.read(magic, 4);
    if (!probe) {
        WavDecodeResult result;
        result.error = "cannot open file";
        return result;
    }
    probe.close();
    if (std::memcmp(magic, "fLaC", 4) == 0)
        return decodeFlacFile(path, out);
    return decodeWavFile(path, out);
}

WavDecodeResult decodeWavFile(const std::filesystem::path& path, SampleData& out)
{
    WavDecodeResult result;

    std::ifstream file(path, std::ios::binary);
    if (!file) { result.error = "cannot open file"; return result; }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (bytes.size() < 44) { result.error = "file too small to be a WAV"; return result; }
    if (bytes.size() > kMaxWavBytes) { result.error = "file exceeds size cap"; return result; }

    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        result.error = "not a RIFF/WAVE file";
        return result;
    }

    uint16_t format = 0, channels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    const uint8_t* dataPtr = nullptr;
    uint64_t dataBytes = 0;
    bool haveFmt = false;

    LoopMetadata loop;

    // Chunk walk — tolerant of malformed sizes: every read is bounds-checked.
    uint64_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const char* id = reinterpret_cast<const char*>(bytes.data() + pos);
        uint64_t size = readU32(bytes.data() + pos + 4);
        const uint64_t body = pos + 8;
        if (size > bytes.size() || body + size > bytes.size())
            size = bytes.size() - body;  // clamp truncated final chunk

        if (std::memcmp(id, "fmt ", 4) == 0 && size >= 16) {
            const uint8_t* f = bytes.data() + body;
            format = readU16(f);
            channels = readU16(f + 2);
            sampleRate = readU32(f + 4);
            bitsPerSample = readU16(f + 14);
            if (format == 0xFFFE && size >= 40)  // WAVE_FORMAT_EXTENSIBLE
                format = readU16(f + 24);        // first 2 bytes of SubFormat GUID
            haveFmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            dataPtr = bytes.data() + body;
            dataBytes = size;
        } else if (std::memcmp(id, "smpl", 4) == 0 && size >= 36 + 24) {
            const uint8_t* s = bytes.data() + body;
            const uint32_t loopCount = readU32(s + 28);
            if (loopCount >= 1 && size >= 36 + 24) {
                const uint8_t* l = s + 36;
                loop.hasLoop = true;
                loop.start = readU32(l + 8);
                loop.end = readU32(l + 12);
            }
        }
        pos = body + size + (size & 1);  // chunks are word-aligned
    }

    if (!haveFmt) { result.error = "missing fmt chunk"; return result; }
    if (dataPtr == nullptr || dataBytes == 0) { result.error = "missing data chunk"; return result; }
    if (channels == 0 || channels > 64) { result.error = "unsupported channel count"; return result; }
    if (sampleRate < 1000 || sampleRate > 384000) { result.error = "implausible sample rate"; return result; }

    const uint32_t bytesPerSample = bitsPerSample / 8u;
    if (bytesPerSample == 0) { result.error = "invalid bit depth"; return result; }
    const uint64_t frameBytes = uint64_t(bytesPerSample) * channels;
    const uint64_t frames = dataBytes / frameBytes;
    if (frames == 0 || frames > kMaxFrames) { result.error = "invalid frame count"; return result; }

    const bool isFloat = (format == 3);
    const bool isPcm = (format == 1);
    if (!isFloat && !isPcm) { result.error = "unsupported WAV format tag"; return result; }
    if (isPcm && bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32) {
        result.error = "unsupported PCM bit depth";
        return result;
    }
    if (isFloat && bitsPerSample != 32 && bitsPerSample != 64) {
        result.error = "unsupported float bit depth";
        return result;
    }

    const uint32_t outChannels = std::min<uint32_t>(channels, 2);
    out.sampleRate = sampleRate;
    out.channels = outChannels;
    out.frames = frames;
    out.data.assign(outChannels, std::vector<float>(size_t(frames), 0.0f));

    for (uint64_t i = 0; i < frames; ++i) {
        const uint8_t* frame = dataPtr + i * frameBytes;
        for (uint32_t c = 0; c < outChannels; ++c) {
            const uint8_t* s = frame + uint64_t(c) * bytesPerSample;
            float v = 0.0f;
            if (isFloat) {
                if (bitsPerSample == 32) { float f; std::memcpy(&f, s, 4); v = f; }
                else                     { double d; std::memcpy(&d, s, 8); v = float(d); }
            } else {
                switch (bitsPerSample) {
                    case 8:  v = (float(s[0]) - 128.0f) / 128.0f; break;
                    case 16: v = float(int16_t(readU16(s))) / 32768.0f; break;
                    case 24: v = pcm24ToFloat(s); break;
                    case 32: v = float(int32_t(readU32(s))) / 2147483648.0f; break;
                    default: break;
                }
            }
            if (!std::isfinite(v)) v = 0.0f;
            out.data[c][size_t(i)] = v;
        }
    }

    // Validate embedded loop against real bounds before trusting it.
    if (loop.hasLoop && loop.start < loop.end && uint64_t(loop.end) < frames)
        out.embeddedLoop = loop;
    else
        out.embeddedLoop = {};

    double sumSq = 0.0;
    float peak = 0.0f;
    for (uint32_t c = 0; c < outChannels; ++c)
        for (float v : out.data[c]) {
            peak = std::max(peak, std::abs(v));
            sumSq += double(v) * v;
        }
    out.peak = peak;
    out.rms = float(std::sqrt(sumSq / double(std::max<uint64_t>(1, frames * outChannels))));

    result.ok = true;
    return result;
}

bool writeWavFile(const std::filesystem::path& path,
                  const float* left, const float* right, uint64_t frames,
                  uint32_t sampleRate, bool asFloat32)
{
    if (left == nullptr || frames == 0) return false;
    const uint16_t channels = (right != nullptr) ? 2 : 1;
    const uint16_t bits = asFloat32 ? 32 : 16;
    const uint16_t formatTag = asFloat32 ? 3 : 1;
    const uint32_t byteRate = sampleRate * channels * (bits / 8);
    const uint16_t blockAlign = uint16_t(channels * (bits / 8));
    const uint64_t dataBytes = frames * blockAlign;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    auto w16 = [&](uint16_t v) { file.put(char(v & 0xFF)); file.put(char(v >> 8)); };
    auto w32 = [&](uint32_t v) {
        file.put(char(v & 0xFF)); file.put(char((v >> 8) & 0xFF));
        file.put(char((v >> 16) & 0xFF)); file.put(char((v >> 24) & 0xFF));
    };

    file.write("RIFF", 4); w32(uint32_t(36 + dataBytes)); file.write("WAVE", 4);
    file.write("fmt ", 4); w32(16);
    w16(formatTag); w16(channels); w32(sampleRate); w32(byteRate); w16(blockAlign); w16(bits);
    file.write("data", 4); w32(uint32_t(dataBytes));

    for (uint64_t i = 0; i < frames; ++i) {
        for (uint16_t c = 0; c < channels; ++c) {
            const float v = std::clamp((c == 0 ? left : right)[i], -1.0f, 1.0f);
            if (asFloat32) {
                uint32_t u; std::memcpy(&u, &v, 4); w32(u);
            } else {
                w16(uint16_t(int16_t(std::lround(v * 32767.0f))));
            }
        }
    }
    return file.good();
}

} // namespace sapp::sounds
