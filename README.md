# SappSounds

<!-- UPDATE WHEN: the one-line description changes, or the repo's top-level layout changes -->

Framework-independent C++20 sample-instrument engine. SappSounds owns every
*generic* sampler concern — SFZ parsing, immutable instrument definitions,
sample decoding, region selection, round robin, keyswitches, release triggers,
loops, resampling, voice allocation, and realtime-safe playback — so products
like [SappOrchestra](https://github.com/localteampartners/sapporchestra) only
add product policy on top.

```text
Repository:     sappsounds
CMake target:   SappSounds   (alias Sapp::Sounds)
C++ namespace:  sapp::sounds
Public headers: include/sapp/sounds/
Artifact:       libSappSounds.a / SappSounds.lib
```

No JUCE, no plugin concepts, no product opinions. The library builds with
CMake ≥ 3.24 and any C++20 compiler; its only dependency is `std::thread`.

## Consumers

```cmake
# Sibling checkout (local development — the default for SappOrchestra):
add_subdirectory(../sappsounds ${CMAKE_BINARY_DIR}/SappSounds)
target_link_libraries(MyProduct PRIVATE Sapp::Sounds)
```

FetchContent, git submodule, and `cmake --install` (exported `Sapp::`
package) are also supported — see [docs/integration.md](docs/integration.md).

## Layout

```text
include/sapp/sounds/   public API (the only headers consumers may include)
src/                   private implementation
tests/unit/            Catch2 suite (45 cases, incl. zero-allocation guards)
tools/                 SappSoundsSFZValidator · SappSoundsInstrumentInspector
                       · SappSoundsRenderTool
docs/                  public_api · realtime_safety · sfz_support · streaming
                       · integration
architecture.md        design document
```

## Build & test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/SappSoundsTests
```

Or just `./verify.sh`.

## Tools

```bash
# Validate an SFZ instrument (exit 0/1/2 = ok/warnings/unplayable):
./build/SappSoundsSFZValidator Violin.sfz --json

# Machine-readable capability report (key range, articulations, keyswitches,
# velocity layers, round robins, RAM estimate):
./build/SappSoundsInstrumentInspector Violin.sfz

# Deterministic offline render:
./build/SappSoundsRenderTool --sfz Violin.sfz --midi phrase.mid --out out.wav

# No samples on disk? Every tool accepts --diagnostic to use the generated
# in-memory "Diagnostic Orchestra" (3 keyswitched articulations).
```

## Project docs

Working state, decisions, and roadmap live in [`_project/`](_project/) —
start with [`_project/CURRENT_STATE.md`](_project/CURRENT_STATE.md).
If you're an agent opening this repo, read [CLAUDE.md](CLAUDE.md) first.
