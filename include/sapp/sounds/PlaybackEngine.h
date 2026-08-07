#pragma once
// Realtime-safe sample playback engine.
//
// Threading contract:
//   * prepare(), setInstrument(), reset*() — control thread (message thread).
//   * process() — audio thread. Never allocates, locks, or touches files.
//   * Instrument snapshots are swapped at block boundaries; retired
//     snapshots are released on the control thread (collectRetired()).
//
// The engine is product-neutral: it applies exactly what regions specify
// (velocity curves, envelopes, loops, keyswitches, CC conditions). Product
// policy — orchestral dynamics on CC1, expression on CC11, spatialization —
// belongs to the consumer (e.g. SappOrchestra).

#include <cstdint>
#include <memory>

#include "DiagnosticSnapshot.h"
#include "InstrumentDefinition.h"
#include "Types.h"

namespace sapp::sounds {

struct EngineConfig {
    int maxVoices = 96;
    int maxBlockFrames = 4096;
    float pitchBendRangeSemitones = 2.0f;
    // Per-note random detune breadth in cents (product "humanize"/DNA hook).
    float randomTuneCents = 0.0f;
    uint32_t seed = 0x5A9F00D5;
    // Resampling: 0 = linear (draft), 1 = cubic Catmull-Rom (normal/high).
    int interpolationQuality = 1;
    float stealFadeSeconds = 0.003f;
};

struct MidiEvent {
    enum class Type : uint8_t { NoteOn, NoteOff, Controller, PitchBend, AllNotesOff, AllSoundOff };
    Type type = Type::NoteOn;
    uint32_t frame = 0;       // offset within the current block
    uint8_t note = 0;         // note number, or controller number
    uint8_t value = 0;        // velocity, or controller value
    uint8_t channel = 0;      // MIDI channel 0-15 (PlaybackEngine itself is
                              // omni; multitimbral consumers route by this)
    int16_t bend14 = 0;       // pitch bend, -8192..8191
};

class PlaybackEngine {
public:
    PlaybackEngine();
    explicit PlaybackEngine(EngineConfig config);
    ~PlaybackEngine();

    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    // --- control thread -----------------------------------------------------
    void prepare(double sampleRate, int maxBlockFrames);
    void setInstrument(InstrumentPtr instrument);   // takes effect next block
    void collectRetired();                          // release retired snapshots
    InstrumentPtr currentInstrument() const;        // last snapshot set

    void setInterpolationQuality(int quality);      // 0 linear, 1 cubic
    void setRandomTuneCents(float cents);
    // Legato level 2: overlapping single-line note-ons skip the new note's
    // recorded attack (skipSeconds into the sample, fadeSeconds attack) and
    // fade the previous note over fadeSeconds. Chord-guarded (30 ms window).
    // Safe to call from any thread, including per-block from the audio thread.
    void setLegato(bool enabled, float skipSeconds = 0.06f, float fadeSeconds = 0.045f) noexcept;
    void resetSequences();                          // round-robin counters
    void reseed(uint32_t seed);

    // --- audio thread -------------------------------------------------------
    // Renders `frames` samples ADDITIVELY into outL/outR (callers clear or mix).
    // `events` must be sorted by frame. Realtime-safe.
    void process(const MidiEvent* events, int eventCount,
                 float* outL, float* outR, int frames) noexcept;

    // --- diagnostics (any thread) ------------------------------------------
    const DiagnosticPublisher& diagnostics() const;
    int activeVoiceCount() const noexcept;          // approximate, lock-free

    const EngineConfig& config() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sapp::sounds
