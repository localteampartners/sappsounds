# SappSounds Architecture

**Status:** implemented, v0.1.0
**Scope:** generic, reusable sample-instrument engine. No product policy.

SappSounds exists so that every Sapp instrument (SappOrchestra today; drum
samplers, pianos, granular instruments, hybrid sample+synth products later)
shares exactly one implementation of the hard sampler problems. Dependency
direction is strict:

```text
SappOrchestra (and future products)
      │  links Sapp::Sounds
      ▼
SappSounds        ← never includes or references any product header
```

## 1. Module map

```text
include/sapp/sounds/          public API
├── Types.h                   ids, enums, Diagnostic, limits
├── SampleData.h              decoded audio + loop/peak metadata (immutable)
├── InstrumentDefinition.h    Region/Articulation/Instrument + LoadedInstrument
├── SfzParser.h               SFZ text → InstrumentDefinition
├── InstrumentLoader.h        parse + resolve + decode → LoadedInstrument
├── PlaybackEngine.h          realtime-safe playback (the audio-thread API)
├── DiagnosticSnapshot.h      seqlock X-Ray feed (voices, note decisions)
├── DiagnosticInstrument.h    generated in-memory test instrument
├── MidiFile.h                SMF 0/1 reader (tools/offline only)
├── OfflineRender.h           deterministic instrument+MIDI → buffers
└── WavIo.h                   WAV decode/encode (off-audio-thread utility)

src/                          private: lexer, voice engine, snapshot exchange
```

Parser internals (tokens, opcode maps), the voice struct, resampler state,
and the snapshot-exchange mechanics are deliberately **not** public.

## 2. Data model

`InstrumentDefinition` is built off the audio thread (SFZ parse or in-memory
generation), then paired with decoded `SampleData` into a `LoadedInstrument`
and frozen. The audio thread only ever sees `std::shared_ptr<const
LoadedInstrument>` snapshots — nothing it reads is ever mutated.

Regions carry the full Phase-1 SFZ surface: key/velocity ranges, root key,
tune/transpose/volume/pan, amp-envelope (DAHDSR), loop definition, sequence
(round-robin) position, random range, trigger mode, group/off_by chokes,
keyswitch conditions (`sw_lokey/hikey/last/default/label`), and CC-range
conditions. Articulations are *derived facts* (grouped by `sw_last`) that
products may use to build their own articulation policy.

## 3. Instrument loading

```text
SFZ file → SfzParser (lexer → #include/#define preprocessor → header parser
         → opcode resolver with global←master←group←region inheritance
         → validation + diagnostics)
       → InstrumentLoader (default_path handling, case-tolerant sample
         resolution, bounded-concurrency WAV decode, missing-sample report)
       → immutable LoadedInstrument
```

Design rules (see docs/sfz_support.md for the opcode table):

- Library files are untrusted: include depth/cycle guards, size caps, region
  caps, token caps, clamped numerics, malformed-WAV tolerance.
- One malformed instrument never fails a library; one missing sample never
  fails an instrument (the region is disabled and reported).
- Unsupported opcodes are recorded and reported, never fatal.

## 4. Playback engine

`PlaybackEngine::process(events, count, outL, outR, frames)` renders
additively into caller buffers and is **realtime-safe**: no allocation, no
locks, no filesystem, no exceptions. Enforced by unit tests that override
`operator new` and count allocations during `process()`.

Inside one block:

1. Adopt any pending instrument snapshot (lock-free exchange; retired
   snapshots are reclaimed later on the control thread via `collectRetired()`).
2. Split the block at event frames; handle note-on/off, CC, pitch bend,
   pedal (CC64, incl. deferred releases + release-sample timing), CC120/123.
3. Note-on: candidate regions come from a per-note lookup table compiled at
   `setInstrument()` time (no linear region scans). Candidates are filtered
   by velocity, keyswitch state, CC conditions, sequence position (per-note
   deterministic round-robin counters), seeded random range, and trigger
   mode (first/legato). Every decision is recorded into the X-Ray snapshot
   with a per-region rejection reason.
4. Voices render with linear (draft) or Catmull-Rom cubic interpolation,
   loop-aware fetches, optional loop crossfade, DAHDSR envelope, equal-power
   pan, and SFZ velocity-curve gain. Stereo samples keep channel identity.
5. Voice stealing: free → quiet release tails → old/quiet → oldest, always
   through a short de-click fade; a stolen voice restarts its pending note
   sample-accurately after the fade.
6. A `DiagnosticSnapshot` (voices, peaks, last note decision, active
   keyswitch) is published through a seqlock — readable from any thread
   without blocking the writer.

Determinism: same instrument + events + sample rate + seed ⇒ identical
output. Round robin uses per-note counters (`resetSequences()` restarts);
random selection and the per-note random-tune hook use a seeded xorshift.

## 5. Threading contract

| Thread | May call |
|---|---|
| Audio | `process()` only |
| Control (message) | `prepare`, `setInstrument`, `collectRetired`, `resetSequences`, `reseed`, setters |
| Any | `diagnostics().read()`, `activeVoiceCount()` |

Snapshot lifecycle: control thread builds → stores in a keep-alive list →
publishes a raw pointer; audio thread exchanges it at a block boundary;
control thread later drops every keep-alive entry that is neither pending
nor active. Destruction therefore always happens off the audio thread.

## 6. Memory & streaming

v0.1 preloads full samples at load time (bounded-concurrency decode).
The streaming architecture — attack-preload + worker-pool read-ahead with
bounded lock-free queues — is specified in docs/streaming.md and is the next
major milestone; the engine's `SampleData`/voice split was designed so that
streamed continuation slots in behind the same fetch interface.

## 7. Testing

45 Catch2 cases: parser (inheritance, includes, defines, spaces, note names,
error locations), selection (velocity boundaries, keyswitches, CC gates,
round-robin determinism + reset, random partitions by seed, first/legato,
chokes, release triggers, pedal), playback (pitch/transpose/SR-conversion
accuracy, loop continuity, release-to-silence, stereo identity, stealing
de-click, NaN guards), loader (default_path, missing samples, case
tolerance, zero-playable failure), WAV (roundtrips, malformed files), and
realtime (zero allocations during `process()`, allocation-free snapshot
adoption).

## 8. Future consumers — design guardrails

Kept deliberately possible, deliberately not implemented here:
sample oscillators for SappSynth (OfflineRender + SampleData are already
usable), drum samplers (group chokes + one-shots exist), pianos (pedal +
release samples exist), granular (SampleData exposes raw frames), standalone
SFZ players (PlaybackEngine + MidiFile suffice). Product features — orchestral
dynamics curves, stage placement, halls, UI — stay out of this repository.
