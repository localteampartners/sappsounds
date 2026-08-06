# Sample Memory & Streaming

## Today (v0.1)

`InstrumentLoader` decodes every referenced sample fully into RAM at load
time using a bounded worker pool (`LoaderOptions::decodeThreads`). The
inspector reports `estimatedRamBytes` so hosts/agents can budget. For the
target free orchestral libraries (VPO, VSCO CE, Sonatina) a single
instrument is typically tens to a few hundred MB — acceptable for a
first release, and completely deterministic.

## Designed next step: hybrid preload + streaming

The voice engine reads samples through a per-voice fetch on an immutable
`SampleData`. Streaming slots in behind that interface without touching
selection, envelopes, or stealing:

```text
Note On
  ├── attack frames read from RAM (pinned preload blocks)
  └── continuation request → streaming worker pool
              ▼
      prepared blocks → bounded lock-free SPSC queue → voice consumes
```

Planned components (in dependency order):

1. **PreloadPlan** — per sample: pinned attack length (ms-based, default
   ~180 ms), computed at load; percussion/short samples stay fully resident.
2. **StreamingService** (public interface) — worker pool with priorities:
   active-voice continuation → new attacks → loop continuation → release
   tails → previews → background analysis.
3. **SampleCache** — memory budget, shared ownership, segmented-LRU
   eviction of non-pinned blocks, hit/miss/underrun counters surfaced
   through `DiagnosticSnapshot`.
4. **Underrun policy** — never block: fade the voice over ~3 ms, count it,
   resume if data arrives; UI notified asynchronously.

## Invariants that must survive streaming

- `process()` stays allocation- and lock-free (queues are preallocated,
  SPSC, bounded).
- Buffer lifetime: blocks released to the reclaimer, never freed on the
  audio thread.
- Determinism holds for offline renders (offline mode reads synchronously).
