cmake_minimum_required(VERSION 3.20)
project(demodoom
    VERSION 0.2.0
    LANGUAGES CXX C
    DESCRIPTION "DeMoDOOM — DOOM-style Faust DSP controller"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2      REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(PIPEWIRE  REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(SPA       REQUIRED IMPORTED_TARGET libspa-0.2)
pkg_check_modules(FAUST     IMPORTED_TARGET faust)
pkg_check_modules(RTMIDI    IMPORTED_TARGET rtmidi)

# nlohmann-json: try cmake config, fallback to header check
find_package(nlohmann_json QUIET)
if(TARGET nlohmann_json::nlohmann_json)
    set(nlohmann_json_FOUND TRUE)
endif()
if(NOT nlohmann_json_FOUND)
    include(CheckIncludeFileCXX)
    check_include_file_cxx("nlohmann/json.hpp" HAS_NLOHMANN_JSON)
    if(HAS_NLOHMANN_JSON)
        set(nlohmann_json_FOUND TRUE)
    endif()
endif()
if(nlohmann_json_FOUND)
    message(STATUS "Found nlohmann-json")
endif()

# ── Sources ───────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp

    # Core
    src/core/engine.cpp
    src/core/preset.cpp

    # Input
    src/input/input_manager.cpp
    src/input/gamepad_device.cpp
    src/input/keyboard_device.cpp
    src/input/bci_device.cpp
    src/input/midi_device.cpp

    # Renderer
    src/renderer/renderer.cpp
    src/renderer/font.cpp
    src/renderer/widgets.cpp

    # Audio
    src/audio/audio_engine.cpp
    src/audio/faust_bridge.cpp
    src/audio/chiptune.cpp
    src/audio/fx_chain.cpp

    # UI Screens
    src/ui/main_menu.cpp
    src/ui/help_screen.cpp
    src/ui/fx_chain_screen.cpp
    src/ui/param_screen.cpp
    src/ui/viz_screen.cpp
    src/ui/settings_screen.cpp
    src/ui/file_browser.cpp
    src/ui/editor_screen.cpp
)

# ── Target ────────────────────────────────────────────────────────────
add_executable(demodoom ${SOURCES})

target_include_directories(demodoom PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(demodoom PRIVATE
    PkgConfig::SDL2
    PkgConfig::PIPEWIRE
    PkgConfig::SPA
    pthread dl m
)

# ── Object library for testable sources (no SDL/PipeWire linkage) ────
add_library(demodoom_core OBJECT
    src/audio/faust_bridge.cpp
    src/audio/chiptune.cpp
    src/audio/fx_chain.cpp
    src/core/preset.cpp
)
target_include_directories(demodoom_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_options(demodoom_core PRIVATE -Wall -Wextra -O2)

if(nlohmann_json_FOUND)
    target_compile_definitions(demodoom_core PUBLIC HAVE_NLOHMANN_JSON=1)
endif()

if(FAUST_FOUND)
    target_compile_definitions(demodoom PRIVATE HAVE_LIBFAUST=1)
    target_link_libraries(demodoom PRIVATE PkgConfig::FAUST)
endif()

if(RTMIDI_FOUND)
    target_compile_definitions(demodoom PRIVATE HAVE_RTMIDI=1)
    target_link_libraries(demodoom PRIVATE PkgConfig::RTMIDI)
endif()

if(nlohmann_json_FOUND)
    target_compile_definitions(demodoom PRIVATE HAVE_NLOHMANN_JSON=1)
    target_link_libraries(demodoom PRIVATE nlohmann_json::nlohmann_json)
endif()

target_compile_options(demodoom PRIVATE
    -Wall -Wextra -Wpedantic -O2
    $<$<CONFIG:Debug>:-g -fsanitize=address>
)
target_link_options(demodoom PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address>
)

install(TARGETS demodoom RUNTIME DESTINATION bin)

# ── Tests ────────────────────────────────────────────────────────────
add_subdirectory(tests)
