#pragma once
// Generated in-memory diagnostic instrument. Lets any SappSounds consumer
// make sound (and run render tests) without redistributing sample content.
//
// Layout: three keyswitched articulations built from additive synthesis —
//   C0  (12): Sustain   — looped, 2 velocity layers, warm harmonic spectrum
//   C#0 (13): Staccato  — short percussive envelope, 2 round robins
//   D0  (14): Pizzicato — plucked exponential decay, 2 round robins
// Multisampled every 7 semitones from C1 (24) to C7 (96).

#include "InstrumentDefinition.h"

namespace sapp::sounds {

struct DiagnosticInstrumentOptions {
    uint32_t sampleRate = 48000;
    float sustainSeconds = 1.5f;   // loop makes it infinite
    float shortSeconds = 0.9f;
    uint32_t seed = 20260806;
};

InstrumentPtr makeDiagnosticInstrument(const DiagnosticInstrumentOptions& options = {});

} // namespace sapp::sounds
