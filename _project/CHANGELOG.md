# CHANGELOG — sappsounds

<!-- UPDATE WHEN: a feature ships or a meaningful fix lands -->

## 2026-08-06 — v0.2.0
- FLAC decode (vendored dr_flac, public domain) + content-sniffing
  decodeAudioFile + .wav<->.flac extension fallback in the loader
- SFZ: off_mode=time + off_time (timed chokes), group/global/master_volume
  (additive), delay (start delay; playback held during envelope delay stage),
  #define substitution inside #include paths
- Validated against Sonatina Symphonic Orchestra: 747 instruments parsed,
  745 playable, ~40k regions, zero errors (remaining warnings are library
  typos and standalone include-partials)

## 2026-08-06 — v0.1.0
- First working release: SFZ Phase-1 parser, WAV decode, instrument loader,
  realtime-safe playback engine (velocity layers, keyswitches, RR, release
  triggers, pedal, chokes, loops, stealing), diagnostic instrument, SMF
  reader, offline renderer, validator/inspector/render tools, 45-case test
  suite with zero-allocation guards.
