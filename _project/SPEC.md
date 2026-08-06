# SPEC — sappsounds

<!-- UPDATE WHEN: goals change, scope changes, non-goals change, or the target user changes -->

## What this is

A framework-independent C++20 static library (`Sapp::Sounds`, namespace
`sapp::sounds`) implementing the generic sample-instrument engine: SFZ
parsing/validation, immutable instrument definitions, sample decoding,
region selection (velocity layers, keyswitches, CC conditions, deterministic
round robin, release triggers, chokes), loop playback, resampling, and a
realtime-safe polyphonic voice engine. Consumers: SappOrchestra (first),
future Sapp instruments (drums, piano, granular, hybrid).

## Why it exists

Every sampler product needs the same hard core; building it once, framework-
free and heavily tested, keeps products (SappOrchestra) thin — they add only
product policy (dynamics curves, stage, hall, UI).

## Goals

- Correct, deterministic sample playback with zero audio-thread allocation
- SFZ Phase-1 opcode coverage that plays real free libraries (VPO, VSCO, Sonatina)
- Small public API; internals private; no JUCE in the core
- Machine-readable tooling (validator/inspector/render) for agents and CI

## Non-goals

- Product features: orchestral dynamics/expression policy, spatialization, UI
- A plugin target (products own wrappers)
- Kontakt/encrypted formats; full SFZ opcode surface (phased instead)
