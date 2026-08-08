# CHANGELOG — sappsounds

<!-- UPDATE WHEN: a feature ships or a meaningful fix lands -->

## 2026-08-08 — v0.3.3

- SFZ parser, ARIA-kit compatibility (unblocks DrumGizmo ports + karoryfer
  kits): multiple `#include` / `#define` directives per line; `#include`
  after a header tag on the same line; nested include paths fall back to
  the top-level .sfz's directory when including-file-relative lookup fails.
- fetch-library.sh: 8 new drum libraries — muldjord-kit, drs-kit,
  naked-drums, virtuosity-drums, swirly-drums, unruly-drums, frankensnare,
  gogodze-phu (all git-cloneable, CC0/CC-BY).

## 2026-08-07 — v0.3.2
- default_path is now positional (SFZ v2 semantics): each <control> header
  re-points it for subsequent regions, baked into region paths at parse
  time. Fixes VSCO2-CE KS patches (multi-articulation combos previously
  reported ~90 missing samples each; now 0). Regression sweeps clean for
  Sonatina and VPO.
- fetch-library.sh: vsco2-ce now overlays the repo's SFZ branch (the CE
  samples ship without mappings) — 75 instruments playable out of the box.

## 2026-08-06 — v0.3.1
- VPO validated: 454/454 instruments playable, 29,993 regions, zero errors
- New opcodes from the VPO sweep: amp_random / pitch_random / delay_random
  (seeded per-note humanize) and gain_ccN (live CC-following region gain —
  makes VPO/Sonatina library-authored CC1 dynamics work as intended)

## 2026-08-06 — v0.3.0
- Dynamic-layer crossfading: xfin/xfout opcodes for velocity, key, and CC —
  CC crossfades are LIVE (voice gain follows the controller with ~8 ms
  smoothing), enabling true CC1 dynamic morphing between layers
- Legato level 2: setLegato() — overlapping single-line note-ons suppress
  the new note's recorded attack (sample-offset skip + short attack) and
  fade the previous note musically; chord-guarded (30 ms window)
- Diagnostic Orchestra sustain layers are now true CC1-crossfading dynamic
  layers
- scripts/fetch-library.sh: curated free-library downloader (sonatina, vpo,
  vsco2-ce, salamander) for new-machine setup

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
