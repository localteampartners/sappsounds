cmake_minimum_required(VERSION 3.24)

project(SappSounds VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Only build tests/tools when SappSounds is the top-level project, so that
# consumers (add_subdirectory / FetchContent) get just the library by default.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(SAPPSOUNDS_STANDALONE ON)
else()
  set(SAPPSOUNDS_STANDALONE OFF)
endif()
option(SAPPSOUNDS_BUILD_TESTS "Build SappSounds unit tests" ${SAPPSOUNDS_STANDALONE})
option(SAPPSOUNDS_BUILD_TOOLS "Build SappSounds developer tools" ${SAPPSOUNDS_STANDALONE})

include(cmake/CompilerWarnings.cmake)

# ---------------------------------------------------------------- library ---
add_library(SappSounds STATIC
  src/WavIo.cpp
  src/FlacDecode.cpp
  src/SfzParser.cpp
  src/InstrumentLoader.cpp
  src/PlaybackEngine.cpp
  src/DiagnosticInstrument.cpp
  src/MidiFile.cpp
  src/OfflineRender.cpp
)
add_library(Sapp::Sounds ALIAS SappSounds)

target_include_directories(SappSounds PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(SappSounds PUBLIC cxx_std_20)
sappsounds_set_warnings(SappSounds)

find_package(Threads REQUIRED)
target_link_libraries(SappSounds PUBLIC Threads::Threads)

# ----------------------------------------------------------------- install --
include(GNUInstallDirs)
install(TARGETS SappSounds EXPORT SappSoundsTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT SappSoundsTargets
  NAMESPACE Sapp::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SappSounds)

# ------------------------------------------------------------------- tests --
if(SAPPSOUNDS_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(Catch2)

  add_executable(SappSoundsTests
    tests/unit/test_wavio.cpp
    tests/unit/test_sfz_parser.cpp
    tests/unit/test_selection.cpp
    tests/unit/test_playback.cpp
    tests/unit/test_realtime.cpp
    tests/unit/test_loader.cpp
  )
  target_link_libraries(SappSoundsTests PRIVATE SappSounds Catch2::Catch2WithMain)
  sappsounds_set_warnings(SappSoundsTests)

  enable_testing()
  add_test(NAME unit COMMAND SappSoundsTests)
endif()

# ------------------------------------------------------------------- tools --
if(SAPPSOUNDS_BUILD_TOOLS)
  foreach(tool IN ITEMS SFZValidator InstrumentInspector RenderTool)
    string(TOLOWER ${tool} toolLower)
    add_executable(SappSounds${tool} tools/${toolLower}/main.cpp)
    target_link_libraries(SappSounds${tool} PRIVATE SappSounds)
    sappsounds_set_warnings(SappSounds${tool})
  endforeach()
endif()
