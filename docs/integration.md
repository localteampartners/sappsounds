# Integrating SappSounds

SappSounds is a plain CMake static library. Four supported consumption modes:

## 1. Sibling checkout (local development — recommended)

```cmake
set(SAPPSOUNDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../sappsounds" CACHE PATH "")
add_subdirectory("${SAPPSOUNDS_DIR}" "${CMAKE_BINARY_DIR}/SappSounds")
target_link_libraries(MyProduct PRIVATE Sapp::Sounds)
```

This is exactly what SappOrchestra does; no network access needed. Tests and
tools are skipped automatically when SappSounds is not the top-level project
(`SAPPSOUNDS_BUILD_TESTS/TOOLS` default OFF in that case).

## 2. FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(SappSounds
  GIT_REPOSITORY https://github.com/localteampartners/sappsounds.git
  GIT_TAG        v0.1.0)
FetchContent_MakeAvailable(SappSounds)
target_link_libraries(MyProduct PRIVATE Sapp::Sounds)
```

## 3. Git submodule

```bash
git submodule add https://github.com/localteampartners/sappsounds.git external/sappsounds
```
```cmake
add_subdirectory(external/sappsounds)
```

## 4. Installed package

```bash
cmake -S sappsounds -B build && cmake --build build && cmake --install build --prefix /opt/sapp
```
```cmake
find_package(SappSounds CONFIG REQUIRED PATHS /opt/sapp/lib/cmake/SappSounds)
target_link_libraries(MyProduct PRIVATE Sapp::Sounds)
```

## Runtime wiring (typical plugin)

```cpp
// message thread
sapp::sounds::InstrumentLoader loader;              // on a worker thread
auto result = loader.loadSfz(path);                  // then hop to message thread:
engine.setInstrument(result.instrument);             // takes effect next block

// audio thread
engine.process(events, count, outL, outR, frames);   // additive render

// occasionally, message thread
engine.collectRetired();
```

Requirements: C++20, CMake ≥ 3.24, Threads. No other dependencies. JUCE is
never required by SappSounds itself.
