# ARCHITECTURE — sappsounds

<!-- UPDATE WHEN: stack changes, components change, data flow changes -->

The authoritative design document is [../architecture.md](../architecture.md).
Quick facts:

- C++20, CMake ≥ 3.24, no dependencies beyond Threads; Catch2 for tests only
- Public API: `include/sapp/sounds/` — everything else private in `src/`
- Pipeline: SfzParser → InstrumentLoader → immutable LoadedInstrument →
  PlaybackEngine (snapshot exchange, per-note lookup tables, preallocated
  voice pool, seqlock diagnostics)
- Realtime rules enforced by allocation-counting unit tests
- Tools: SappSoundsSFZValidator / InstrumentInspector / RenderTool (JSON out)
