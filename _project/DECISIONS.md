# DECISIONS — sappsounds

<!-- UPDATE WHEN: a non-obvious technical choice is made -->

## 2026-08-06 — Separate repo, sibling-directory consumption
SappSounds is its own repository (not a folder in SappOrchestra, not a
`SappAudio` monorepo). Products consume it via `add_subdirectory(../sappsounds)`
locally, FetchContent/submodule/installed-package otherwise. Rationale: strict
dependency direction, independent tests, reuse by future products.

## 2026-08-06 — No JUCE in the core
Decoding, parsing, playback are dependency-free. JUCE stays in product
wrappers. Cost: hand-written WAV/SMF readers (small, capped, tested).

## 2026-08-06 — Full-RAM preload first, streaming second
Correct in-memory playback ships before disk streaming (mirrors the
build-phase rule "do not begin streaming before in-memory playback is
correct"). The voice fetch interface was shaped so streaming slots in later.

## 2026-08-06 — Seqlock for diagnostics
X-Ray data is published from the audio thread via a version-counter seqlock
(single writer, tear-checked readers) instead of queues — constant cost while
closed, no allocation ever.

## 2026-08-06 — Per-note round-robin counters
`seq_position` matching uses a per-MIDI-note counter (sforzando-like
behavior), reset via `resetSequences()`. Deterministic under a fixed seed.

## 2026-08-06 — Catch2 v3.7.1 pinned; JUCE not referenced
Matches sappsynth's test stack for consistency across the suite.

## 2026-10-10 — Drum samples: SampleRadar for machines, CC0 sources for the Latin kit

- **Electronic one-shots come from SampleRadar** (MusicRadar). Its licence is
  explicit on every pack page ("royalty-free ... use the samples in your music
  in any way you like - all we ask is that you don't re-distribute them"), the
  zips are direct CDN URLs with no login, and the content is the real
  machines (TR-808/909/707/505, TR-8, LinnDrum-era kits, vinyl kits). The
  price is no redistribution: they are fetched per box, never mirrored.
- **Rejected:** Freesound (login + API key for originals), Reverb Drum Machines
  (email gate), Synthwave Dojo 80s machines (signup), itch.io/Gumroad packs
  (JS download flow), Philharmonia (MP3 only), Hydrogen kits and
  tidal/strudel drum-machine collections (provenance/licence not stated by
  the recorder), the hyperreal TR-808 set ("no licensing restrictions" is not
  a licence), MuseScore_General percussion (fine licence, but GM-soundfont
  quality and single-velocity — Virtuosity covered the same notes better).
- **The Latin kit is CC0 only** (Virtuosity + VCSL + FreePats + Body
  Percussion), so it could be re-hosted as one zip later; the mapping is ours
  and committed, the samples are fetched from their authors.
- **Gaps with no vetted free source:** timbale paila/cascara (shell) strokes,
  a true conga slap, real surdo/pandeiro/tamborim recordings (approximated:
  concert bass drum + mallet tom for surdo, frame drum + tambourine jingles
  for pandeiro, a tuned-up choked frame drum for tamborim).
