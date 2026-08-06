#pragma once
// Compact engine diagnostics ("X-Ray" feed). Written by the audio thread
// with a seqlock; read by UI/tools without blocking the writer.

#include <atomic>
#include <cstdint>

#include "Types.h"

namespace sapp::sounds {

enum class RejectReason : uint8_t {
    None,          // selected
    Velocity,
    Keyswitch,
    CcCondition,
    Sequence,      // round-robin position
    Random,
    TriggerMode,
    Polyphony
};

struct RegionDecision {
    RegionIndex region = 0;
    RejectReason reason = RejectReason::None;
};

struct NoteDecision {
    static constexpr int kMaxRecorded = 48;
    uint64_t engineFrame = 0;
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint8_t candidateCount = 0;   // candidates considered (clamped to kMaxRecorded)
    uint8_t selectedCount = 0;
    int8_t activeKeyswitch = -1;
    bool wasKeyswitch = false;    // the note itself was a keyswitch press
    RegionDecision decisions[kMaxRecorded] = {};
};

struct DiagnosticSnapshot {
    uint32_t activeVoices = 0;
    uint32_t peakVoices = 0;
    uint32_t voicesStolen = 0;      // lifetime counter
    uint32_t notesOn = 0;           // lifetime counter
    float lastPeakL = 0.0f;
    float lastPeakR = 0.0f;
    int8_t activeKeyswitch = -1;
    NoteDecision lastNote;          // most recent note-on decision
};

// Single-writer (audio thread) / multi-reader snapshot cell.
class DiagnosticPublisher {
public:
    // Audio thread: publish a new snapshot (no locks, no allocation).
    void publish(const DiagnosticSnapshot& s) noexcept
    {
        seq_.fetch_add(1, std::memory_order_release);  // odd: writing
        snapshot_ = s;
        seq_.fetch_add(1, std::memory_order_release);  // even: stable
    }

    // Any thread: read a coherent snapshot. Returns false if torn twice.
    bool read(DiagnosticSnapshot& out) const noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt) {
            const uint32_t before = seq_.load(std::memory_order_acquire);
            if (before & 1u) continue;
            out = snapshot_;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == before) return true;
        }
        return false;
    }

private:
    std::atomic<uint32_t> seq_{0};
    DiagnosticSnapshot snapshot_{};
};

} // namespace sapp::sounds
