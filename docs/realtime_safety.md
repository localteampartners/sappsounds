# Realtime Safety

The audio thread runs exactly one SappSounds entry point:
`PlaybackEngine::process()`. Inside it the engine must never:

- allocate or free memory
- lock a mutex or wait on any synchronization primitive
- parse SFZ or JSON
- touch the filesystem
- resize containers
- log synchronously
- throw exceptions

## How each rule is met

| Concern | Mechanism |
|---|---|
| Voices | Preallocated pool (`EngineConfig::maxVoices`), fixed-size `Voice` structs |
| Instrument swap | Control thread builds + compiles the snapshot; audio thread adopts via a single `std::atomic` exchange; old snapshots reclaimed by `collectRetired()` on the control thread |
| Region lookup | Per-note candidate tables compiled at `setInstrument()` time (control thread) |
| Note decisions | Fixed-size arrays (`kMaxSelectedPerNote`, `NoteDecision::kMaxRecorded`) |
| Diagnostics | Seqlock (`DiagnosticPublisher`): lock-free single writer, tear-checked readers |
| Voice stealing | In-place scoring over the pool; de-click fades reuse the voice's own state |
| Parameters | Consumers pass plain values / atomics; the engine reads, never blocks |

## Enforcement

`tests/unit/test_realtime.cpp` overrides global `operator new/delete` and
counts allocations while `process()` runs 200 busy blocks (chords,
keyswitch, pedal, bend, releases) — the count must be zero. A second case
proves adopting a staged instrument swap on the audio thread is also
allocation-free.

These tests are part of the default suite; a regression fails CI, not a
listening test.

## Contract for consumers

- Call `process()` from exactly one thread at a time.
- Call `prepare()`/`setInstrument()`/`collectRetired()` from the control
  (message) thread only.
- `setInstrument(nullptr)` silences safely; voices from a replaced snapshot
  fade out within a block.
