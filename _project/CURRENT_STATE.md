# CURRENT STATE — sappsounds

<!-- UPDATE WHEN: a feature ships, something breaks, or a known issue is found/fixed -->

**As of 2026-08-06 — v0.2.0: real-library validated.**

## Working

- SFZ parser: structure, includes/defines, inheritance, Phase-1 opcodes,
  diagnostics with file:line, unsupported-opcode reporting
- WAV decode (PCM 8/16/24/32, float32/64, smpl loops) + encode; FLAC decode
  via vendored dr_flac (public domain); .wav<->.flac extension fallback
- InstrumentLoader: default_path, case-tolerant paths, parallel decode,
  missing-sample tolerance
- PlaybackEngine: velocity layers, keyswitches, CC conditions, deterministic
  RR + random, release triggers, sustain pedal, group chokes, loops with
  crossfade, linear/cubic resampling, priority stealing with de-click,
  snapshot swap, seqlock X-Ray diagnostics
- Diagnostic Orchestra generated instrument (3 keyswitched articulations)
- SMF 0/1 reader, deterministic offline renderer
- 3 CLI tools with JSON output; 45-case test suite green (incl. zero-alloc guards)

## Known issues / limits

- Samples are fully preloaded to RAM (no disk streaming yet — see docs/streaming.md)
- No AIFF decode yet (WAV + FLAC covered)
- Phase-2 opcodes (filters, LFOs, crossfades, rt_decay) unimplemented, reported as unsupported
- Sinc resampling mode not yet implemented (linear/cubic only)
- Validated against Sonatina Symphonic Orchestra (747 SFZ files, 745
  playable, 40k regions, 0 errors); VPO download link still to be sourced
