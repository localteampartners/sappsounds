#pragma once
// Deterministic offline rendering: instrument + timed MIDI events → audio.
// For tools, tests, and product bounce paths. Not for the audio thread.

#include <cstdint>
#include <vector>

#include "InstrumentDefinition.h"
#include "MidiFile.h"
#include "PlaybackEngine.h"

namespace sapp::sounds {

struct OfflineRenderOptions {
    double sampleRate = 48000.0;
    int blockFrames = 512;
    double tailSeconds = 3.0;     // rendered after the last event
    int interpolationQuality = 1; // 0 linear, 1 cubic
    uint32_t seed = 0x5A9F00D5;
    float randomTuneCents = 0.0f;
    int maxVoices = 128;
    float masterGain = 1.0f;
};

struct RenderOutput {
    std::vector<float> left, right;
    double sampleRate = 48000.0;
    float peak = 0.0f;
    float rms = 0.0f;
};

RenderOutput renderOffline(const InstrumentPtr& instrument,
                           const std::vector<TimedMidiEvent>& events,
                           const OfflineRenderOptions& options = {});

} // namespace sapp::sounds
