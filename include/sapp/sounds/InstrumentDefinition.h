#pragma once
// Immutable instrument model. Built off the audio thread by the SFZ parser /
// InstrumentLoader (or generated in memory), then handed to PlaybackEngine
// as part of a LoadedInstrument snapshot. Never mutated after load.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "SampleData.h"
#include "Types.h"

namespace sapp::sounds {

struct LoopDefinition {
    LoopMode mode = LoopMode::NoLoop;
    bool explicitMode = false;   // loop_mode opcode was present
    int64_t start = -1;          // -1: use embedded smpl loop if present
    int64_t end = -1;            // inclusive last loop frame
    float crossfadeSeconds = 0.0f;  // converted to frames at voice start
};

// SFZ ampeg_* model. Times in seconds, sustain 0..1, start 0..1.
struct EnvelopeDefinition {
    float delay = 0.0f;
    float start = 0.0f;
    float attack = 0.0f;
    float hold = 0.0f;
    float decay = 0.0f;
    float sustain = 1.0f;
    float release = 0.05f;
};

struct RegionDefinition {
    SampleIndex sample = kInvalidSample;
    std::string samplePath;      // relative to instrument source (or generated id)

    uint8_t loKey = 0, hiKey = 127, rootKey = 60;
    uint8_t loVel = 0, hiVel = 127;

    int transpose = 0;           // semitones
    float tuneCents = 0.0f;
    float volumeDb = 0.0f;
    float extraVolumeDb = 0.0f;  // additive scope volumes (group_volume, …)
    float pan = 0.0f;            // -100..100
    float ampVeltrack = 100.0f;  // percent, may be negative

    int64_t offset = 0;          // start frame within sample
    int64_t end = -1;            // inclusive end frame, -1 = natural end

    LoopDefinition loop;
    EnvelopeDefinition ampeg;

    // Round robin / random selection
    uint16_t seqLength = 1;
    uint16_t seqPosition = 1;
    float loRand = 0.0f;
    float hiRand = 1.0f;

    TriggerMode trigger = TriggerMode::Attack;
    int32_t group = 0;
    int32_t offBy = 0;
    OffMode offMode = OffMode::Fast;
    float offTime = 0.006f;      // choke fade for off_mode=time, seconds
    int notePolyphony = 0;       // 0 = unlimited

    // Keyswitch conditions
    int swLoKey = -1, swHiKey = -1;  // keyswitch key range for this instrument
    int swLast = -1;                 // region active while last keyswitch == swLast
    int swDefault = -1;
    std::string swLabel;

    // MIDI CC range conditions (locc/hicc)
    std::vector<CcCondition> ccConditions;

    // Crossfade ranges (xfin_*/xfout_*). -1 = unset. CC crossfades are LIVE:
    // the voice gain follows the controller while the note sounds — this is
    // what makes CC1 dynamic-layer morphing work. Velocity/key crossfades are
    // static per note-on. Curves: 0 = power (equal-power sqrt), 1 = gain.
    struct CcCrossfade {
        uint8_t cc = 0;
        int16_t inLo = -1, inHi = -1, outLo = -1, outHi = -1;
    };
    std::vector<CcCrossfade> ccCrossfades;
    int16_t xfinLoVel = -1, xfinHiVel = -1, xfoutLoVel = -1, xfoutHiVel = -1;
    int16_t xfinLoKey = -1, xfinHiKey = -1, xfoutLoKey = -1, xfoutHiKey = -1;
    uint8_t xfCcCurve = 0, xfVelCurve = 0, xfKeyCurve = 0;

    // Live CC gain (gain_ccN): region gain scaled by db * cc/127, following
    // the controller while the note sounds (library-authored CC dynamics).
    struct CcGain {
        uint8_t cc = 0;
        float db = 0.0f;
    };
    std::vector<CcGain> gainCc;

    // Per-note humanize (deterministic under the engine seed):
    // symmetric ±/2 for amp/pitch, additive [0, v] for delay.
    float ampRandomDb = 0.0f;
    float pitchRandomCents = 0.0f;
    float delayRandomSeconds = 0.0f;

    uint32_t sourceLine = 0;     // line of <region> header in the SFZ source
};

// Articulations are derived (from keyswitch structure) or product-defined.
// SappSounds reports them; product layers (e.g. SappOrchestra) decide policy.
struct ArticulationInfo {
    std::string name;            // sw_label or generated ("Keyswitch C#0")
    int keyswitch = -1;          // trigger note, -1 for "always on"
    uint32_t regionCount = 0;
    bool isDefault = false;
};

struct ControlLabel {
    uint8_t cc = 0;
    std::string label;
};

struct ControlDefault {
    uint8_t cc = 0;
    uint8_t value = 0;
};

struct InstrumentDefinition {
    std::string name;
    std::string sourcePath;      // absolute SFZ path ("" for generated)
    std::string defaultPath;     // <control> default_path, already applied

    std::vector<RegionDefinition> regions;
    std::vector<ArticulationInfo> articulations;
    std::vector<ControlLabel> controlLabels;      // label_ccN
    std::vector<ControlDefault> controlDefaults;  // set_ccN

    // Playable range (excluding keyswitch keys), computed at load.
    uint8_t loKeyUsed = 127, hiKeyUsed = 0;
    int keyswitchLo = -1, keyswitchHi = -1;       // union of sw_lokey/sw_hikey
    int defaultKeyswitch = -1;

    // Parse bookkeeping
    std::vector<std::string> unsupportedOpcodes;  // unique, sorted
    uint32_t unsupportedOpcodeHits = 0;
    uint32_t recognizedOpcodeHits = 0;
};

// A fully loaded, immutable instrument: definition + decoded samples.
// Shared by pointer; the audio thread only ever sees complete snapshots.
struct LoadedInstrument {
    InstrumentDefinition definition;
    std::vector<SampleData> samples;

    uint64_t sampleBytes() const noexcept
    {
        uint64_t total = 0;
        for (const auto& s : samples) total += s.bytes();
        return total;
    }
};

using InstrumentPtr = std::shared_ptr<const LoadedInstrument>;

} // namespace sapp::sounds
