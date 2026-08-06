# ROADMAP — sappsounds

<!-- UPDATE WHEN: direction, sequence, or theme changes -->

## Theme 1 (now): Real libraries
Prove VPO/VSCO/Sonatina end-to-end; fix parser/loader gaps they expose.

## Theme 2: Streaming
Attack preload + worker-pool streaming + cache budget (docs/streaming.md).
Exit criterion: long sustains from SSD with zero underruns in stress tests.

## Theme 3: Depth
Phase-2 opcodes (filters, crossfades, rt_decay), sinc resampling, MPE,
library indexer tool, cross-platform CI + benchmarks.

## Theme 4: More consumers
SappSynth sample oscillators; drum/piano product cores as thin layers.
