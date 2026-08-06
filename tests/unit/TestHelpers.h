#pragma once
// Shared fixtures for SappSounds unit tests: in-memory instruments,
// tiny generated WAV files, and event helpers.

#include <cmath>
#include <filesystem>
#include <vector>

#include "sapp/sounds/InstrumentDefinition.h"
#include "sapp/sounds/PlaybackEngine.h"
#include "sapp/sounds/WavIo.h"

namespace sapptest {

using namespace sapp::sounds;

inline SampleData makeSine(double freq, uint32_t rate, double seconds,
                           uint32_t channels = 1, float amplitude = 0.5f)
{
    SampleData s;
    s.sampleRate = rate;
    s.channels = channels;
    s.frames = uint64_t(rate * seconds);
    s.data.assign(channels, std::vector<float>(size_t(s.frames), 0.0f));
    for (uint64_t i = 0; i < s.frames; ++i) {
        const float v = amplitude * float(std::sin(2.0 * 3.14159265358979 * freq * double(i) / rate));
        for (uint32_t c = 0; c < channels; ++c) s.data[c][size_t(i)] = v;
    }
    s.peak = amplitude;
    return s;
}

// Single-region instrument around a sine sample.
inline std::shared_ptr<LoadedInstrument> makeSineInstrument(
    double freq = 440.0, uint32_t rate = 48000, uint8_t rootKey = 69,
    double seconds = 1.0)
{
    auto inst = std::make_shared<LoadedInstrument>();
    inst->definition.name = "test-sine";
    inst->samples.push_back(makeSine(freq, rate, seconds));
    RegionDefinition r;
    r.sample = 0;
    r.samplePath = "sine.gen";
    r.rootKey = rootKey;
    r.loKey = 0;
    r.hiKey = 127;
    r.ampeg.release = 0.02f;
    inst->definition.regions.push_back(r);
    return inst;
}

inline MidiEvent noteOn(uint32_t frame, uint8_t note, uint8_t vel)
{
    MidiEvent e;
    e.type = MidiEvent::Type::NoteOn;
    e.frame = frame;
    e.note = note;
    e.value = vel;
    return e;
}

inline MidiEvent noteOff(uint32_t frame, uint8_t note)
{
    MidiEvent e;
    e.type = MidiEvent::Type::NoteOff;
    e.frame = frame;
    e.note = note;
    return e;
}

inline MidiEvent controller(uint32_t frame, uint8_t cc, uint8_t value)
{
    MidiEvent e;
    e.type = MidiEvent::Type::Controller;
    e.frame = frame;
    e.note = cc;
    e.value = value;
    return e;
}

// Render helper: run engine for N blocks, return stereo buffers.
struct Rendered {
    std::vector<float> left, right;
};

inline Rendered renderBlocks(PlaybackEngine& engine, const std::vector<MidiEvent>& events,
                             int totalFrames, int blockFrames = 256)
{
    Rendered out;
    out.left.assign(size_t(totalFrames), 0.0f);
    out.right.assign(size_t(totalFrames), 0.0f);
    size_t next = 0;
    for (int start = 0; start < totalFrames; start += blockFrames) {
        const int frames = std::min(blockFrames, totalFrames - start);
        std::vector<MidiEvent> block;
        while (next < events.size() && events[next].frame < uint32_t(start + frames)) {
            MidiEvent e = events[next];
            e.frame = e.frame >= uint32_t(start) ? e.frame - uint32_t(start) : 0;
            block.push_back(e);
            ++next;
        }
        engine.process(block.data(), int(block.size()),
                       out.left.data() + start, out.right.data() + start, frames);
    }
    return out;
}

// Dominant frequency by zero-crossing count (good enough for pure tones).
inline double estimateFrequency(const std::vector<float>& x, double rate,
                                size_t begin, size_t end)
{
    int crossings = 0;
    for (size_t i = begin + 1; i < end && i < x.size(); ++i)
        if ((x[i - 1] <= 0.0f && x[i] > 0.0f)) ++crossings;
    const double seconds = double(end - begin) / rate;
    return crossings / seconds;
}

inline std::filesystem::path tempDir()
{
    auto dir = std::filesystem::temp_directory_path() / "sappsounds-tests";
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace sapptest
