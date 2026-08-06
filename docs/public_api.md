# SappSounds Public API

Everything a consumer may touch lives in `include/sapp/sounds/`, namespace
`sapp::sounds`. Anything under `src/` is private and may change without
notice.

## Core types

| Type | Header | Role |
|---|---|---|
| `SampleData` | SampleData.h | Decoded audio (float32, source rate/channels) + loop/peak metadata |
| `RegionDefinition` | InstrumentDefinition.h | One mapped sample zone (keys, velocities, envelope, loop, RR, triggers, conditions) |
| `ArticulationInfo` | InstrumentDefinition.h | Derived keyswitch articulation facts |
| `InstrumentDefinition` | InstrumentDefinition.h | Immutable instrument model |
| `LoadedInstrument` / `InstrumentPtr` | InstrumentDefinition.h | Definition + decoded samples, shared immutably |
| `Diagnostic` | Types.h | severity + file + line + message |

## Loading (control/background threads)

```cpp
sapp::sounds::SfzParser parser;                 // text → InstrumentDefinition
auto parsed = parser.parseFile("Violin.sfz");   // diagnostics, never throws

sapp::sounds::InstrumentLoader loader;          // parse + decode samples
auto load = loader.loadSfz("Violin.sfz");       // LoadResult{instrument, diagnostics, missingSamples, ok}

auto diag = sapp::sounds::makeDiagnosticInstrument();  // generated content
```

## Playback (realtime)

```cpp
sapp::sounds::PlaybackEngine engine({.maxVoices = 96});
engine.prepare(sampleRate, maxBlock);       // control thread
engine.setInstrument(load.instrument);      // control thread; takes effect next block

// audio thread — renders ADDITIVELY into outL/outR:
engine.process(events, eventCount, outL, outR, frames);

engine.collectRetired();                    // control thread, occasionally
```

`MidiEvent` covers NoteOn/NoteOff/Controller/PitchBend/AllNotesOff/AllSoundOff
with a block-relative `frame`; events must be sorted by frame.

Engine hooks products may use:

- `setInterpolationQuality(0|1)` — linear (draft) / cubic (normal+)
- `setRandomTuneCents(c)` — deterministic per-note detune (humanize/DNA)
- `resetSequences()`, `reseed(seed)` — deterministic round robin / random

## Diagnostics (any thread)

```cpp
sapp::sounds::DiagnosticSnapshot snap;
if (engine.diagnostics().read(snap)) {
    snap.activeVoices; snap.lastPeakL; snap.activeKeyswitch;
    snap.lastNote;   // note, velocity, candidates with per-region RejectReason
}
```

## Offline (tools, tests, bounces)

```cpp
auto midi = sapp::sounds::readMidiFile("phrase.mid");     // SMF 0/1
auto out  = sapp::sounds::renderOffline(instrument, midi.events,
                                        {.sampleRate = 48000.0, .seed = 42});
sapp::sounds::writeWavFile("out.wav", out.left.data(), out.right.data(),
                           out.left.size(), 48000);
```

## Stability

- The public headers form the compatibility surface; parser tokens, voice
  internals, cache/streaming internals stay private.
- No JUCE (or any framework) types appear in the public API.
- Enum values and struct fields may be added; existing ones are not
  repurposed.
