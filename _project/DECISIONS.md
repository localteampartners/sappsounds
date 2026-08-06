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
