#pragma once
// SappSounds — framework-independent sample-instrument engine.
// Public basic types. No JUCE, no product concepts.

#include <cstdint>
#include <string>

namespace sapp::sounds {

// Index into LoadedInstrument::samples. Negative means "unresolved/missing".
using SampleIndex = int32_t;
inline constexpr SampleIndex kInvalidSample = -1;

// Index into InstrumentDefinition::regions.
using RegionIndex = uint32_t;

enum class LoopMode : uint8_t {
    NoLoop,      // play to end of sample (default for percussive content)
    OneShot,     // ignore note-off, play to end
    Continuous,  // loop for the life of the voice, including release
    Sustain      // loop while the note (or pedal) is held, then run to end
};

enum class TriggerMode : uint8_t {
    Attack,       // normal note-on
    Release,      // plays on note-off (release samples)
    ReleaseKey,   // plays on note-off even if sustain pedal is down
    First,        // only when no other note is sounding (legato head)
    Legato        // only when another note is already sounding
};

enum class OffMode : uint8_t {
    Fast,   // choked voices are cut with a short de-click fade
    Normal, // choked voices run their normal release
    Time    // choked voices fade over off_time seconds (ARIA extension)
};

struct CcCondition {
    uint8_t cc = 0;
    uint8_t lo = 0;
    uint8_t hi = 127;
};

// Severity for parser / loader diagnostics.
enum class Severity : uint8_t { Info, Warning, Error };

struct Diagnostic {
    Severity severity = Severity::Info;
    std::string file;      // source file (SFZ or sample path)
    int line = 0;          // 0 when not tied to a source line
    std::string message;
};

struct Version {
    static constexpr int major = 0;
    static constexpr int minor = 1;
    static constexpr int patch = 0;
    static const char* string() noexcept { return "0.1.0"; }
};

} // namespace sapp::sounds
