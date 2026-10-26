cmake_minimum_required(VERSION 3.20)
project(citsy VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(CITSY_BUILD_TESTS            "Build unit tests"                 ON)
option(CITSY_BUILD_EXAMPLES         "Build example programs"           ON)
option(CITSY_BUILD_WEB              "Build the Emscripten browser player" OFF)

# ---------------------------------------------------------------------------
# Core library: citsy
# ---------------------------------------------------------------------------
add_library(citsy STATIC
    src/parser/parser.cpp
    src/engine/engine.cpp
    src/dialog/script.cpp
    src/dialog/value.cpp
    src/render/compose.cpp
    src/font/font.cpp
    src/font/builtin_fonts.cpp
    src/sound/sound.cpp
    src/transition/transition.cpp
)

target_include_directories(citsy
    PUBLIC
        # Public headers: #include <citsy/...>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # Internal headers: src/model/game.hpp, src/parser/parser.hpp, …
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(citsy PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Wasm exceptions are required so the web player can catch ParseError.
if(EMSCRIPTEN)
    target_compile_options(citsy PRIVATE -fexceptions)
endif()

# citsy must have ZERO link deps on platform/media libraries.
# Add only header-only or pure-logic dependencies here.

# ---------------------------------------------------------------------------
# Mock backend (header-only interface, referenced from tests/examples)
# ---------------------------------------------------------------------------
# backends/mock/mock_host.hpp is header-only; no separate target needed.
# Tests and examples include it via the source tree include path.

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(CITSY_BUILD_TESTS)
    enable_testing()

    # Fetch Catch2 v3
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4
    )
    FetchContent_MakeAvailable(Catch2)

    # Bitsy 8.15's engine and fonts, which the built-in font test compares
    # against (scripts/fetch_bitsy.cmake). They are not stored in the repo.
    include(${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch_bitsy.cmake)

    add_executable(citsy_tests
        tests/unit/test_parser.cpp
        tests/unit/test_engine.cpp
        tests/unit/test_dialog.cpp
        tests/unit/test_simulation.cpp
        tests/unit/test_phase3.cpp
        tests/unit/test_textbox.cpp
        tests/unit/test_parity.cpp
        tests/unit/test_state.cpp
    )

    target_link_libraries(citsy_tests
        PRIVATE
            citsy
            Catch2::Catch2WithMain
    )

    # The browser player's C API, built natively against a stub emscripten.h.
    target_sources(citsy_tests PRIVATE
        tests/unit/test_web_api.cpp
        backends/web/web_player.cpp
    )

    target_include_directories(citsy_tests
        PRIVATE
            # Allow includes like: #include "src/parser/parser.hpp"
            ${CMAKE_CURRENT_SOURCE_DIR}
            # Allow includes like: #include "backends/mock/mock_host.hpp"
            ${CMAKE_CURRENT_SOURCE_DIR}
            # Stand-in emscripten.h for backends/web/web_player.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs
    )

    include(Catch)
    catch_discover_tests(citsy_tests)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(CITSY_BUILD_EXAMPLES)
    add_executable(minimal_example
        examples/minimal/main.cpp
    )

    target_link_libraries(minimal_example PRIVATE citsy)

    target_include_directories(minimal_example
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# Display backends (32blit, custom hosts, …) live in separate repositories.
# The browser player is the exception: it is an optional Emscripten target
# so the Chili creator can run games without the JavaScript Bitsy engine.

if(CITSY_BUILD_WEB)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "CITSY_BUILD_WEB requires the Emscripten toolchain")
    endif()

    add_executable(citsy_web backends/web/web_player.cpp)
    target_link_libraries(citsy_web PRIVATE citsy)
    target_compile_options(citsy_web PRIVATE -fexceptions -O2)
    set_target_properties(citsy_web PROPERTIES OUTPUT_NAME "citsy")
    target_link_options(citsy_web PRIVATE
        "-fexceptions"
        "SHELL:-sMODULARIZE=1"
        "SHELL:-sEXPORT_NAME=CitsyModule"
        "SHELL:-sENVIRONMENT=web,node"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','HEAPU8','HEAPF32']"
        "SHELL:-sEXPORTED_FUNCTIONS=['_citsy_load','_citsy_unload','_citsy_set_button','_citsy_frame','_citsy_pixels','_citsy_sound','_citsy_last_error','_citsy_state_json','_citsy_state_version','_citsy_set_variable','_citsy_set_item','_citsy_validate']"
    )
endif()
