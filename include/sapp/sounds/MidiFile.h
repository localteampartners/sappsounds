#pragma once
// Minimal Standard MIDI File (format 0/1) reader for tools, tests, and
// offline rendering. Produces a flat, tempo-resolved, time-sorted event list.
// Off-audio-thread use only.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sapp::sounds {

struct TimedMidiEvent {
    double seconds = 0.0;
    uint8_t status = 0;   // 0x80 noteOff, 0x90 noteOn, 0xB0 cc, 0xE0 bend (channel stripped)
    uint8_t channel = 0;
    uint8_t data1 = 0;    // note / cc number
    uint8_t data2 = 0;    // velocity / cc value
    int16_t bend14 = 0;   // for 0xE0, -8192..8191
};

struct MidiFileResult {
    bool ok = false;
    std::string error;
    std::vector<TimedMidiEvent> events;  // sorted by time
    double durationSeconds = 0.0;
};

MidiFileResult readMidiFile(const std::filesystem::path& path);

} // namespace sapp::sounds
