cmake_minimum_required(VERSION 3.16)
project(gba_improvements CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GbaimpFindSDL2)   # defines the INTERFACE target gbaimp_sdl2

# Optional sanitizer for the library and its tests, e.g. -DGBAIMP_SANITIZE=thread (or address, undefined).
set(GBAIMP_SANITIZE "" CACHE STRING "Sanitizer for gba_improvements and its tests (thread, address, undefined; empty = none)")

# Unit tests: on by default only when this is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_gbaimp_tests_default ON)
else()
    set(_gbaimp_tests_default OFF)
endif()
# Parents that want the library tests configure with -DGBAIMP_BUILD_TESTS=ON.
option(GBAIMP_BUILD_TESTS "Build the gbaimp unit tests (ON by default only as top-level project)" ${_gbaimp_tests_default})

add_library(gba_improvements STATIC
    src/hooks.cpp
    src/pixel_art_upscaler.cpp
    src/sdl_interpose.cpp
    src/sdl_interpose_win32.cpp
    src/screen_filters.cpp
    src/frame_interpolator.cpp
    src/audio_dsp.cpp
    src/audio_pipeline.cpp
    src/audio_device.cpp
    src/hd_audio_player.cpp
    src/dialog_turbo.cpp
    src/savestate_thumbnails.cpp
    src/perf_hud.cpp
    src/input_enhancements.cpp
    src/hud_anchoring.cpp
    src/game_host.cpp)
add_library(gbaimp::gba_improvements ALIAS gba_improvements)

target_include_directories(gba_improvements PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(gba_improvements PUBLIC gbaimp_sdl2)
if(GBAIMP_SANITIZE AND NOT MSVC)
    target_compile_options(gba_improvements PUBLIC -fsanitize=${GBAIMP_SANITIZE} -fno-omit-frame-pointer -g)
    target_link_options(gba_improvements PUBLIC -fsanitize=${GBAIMP_SANITIZE})
endif()

# Tools and examples: on by default only when this is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_gbaimp_extras_default ON)
else()
    set(_gbaimp_extras_default OFF)
endif()
option(GBAIMP_BUILD_TOOLS "Build helper tools (verify_rom_hash)" ${_gbaimp_extras_default})
option(GBAIMP_BUILD_EXAMPLES "Build the example programs" ${_gbaimp_extras_default})

# Deprecated: verify_rom_hash no longer needs the host's gbarecomp headers.
if(DEFINED GBAIMP_GBARECOMP_INCLUDE)
    message(STATUS "gba-improvements: GBAIMP_GBARECOMP_INCLUDE is deprecated and ignored "
                   "(verify_rom_hash is self-contained; use GBAIMP_BUILD_TOOLS).")
endif()

target_link_libraries(gba_improvements PUBLIC ${CMAKE_DL_LIBS})

# Self-contained CLI helper: verify_rom_hash <file> <expected_sha1>.
if(GBAIMP_BUILD_TOOLS)
    enable_testing()
    add_executable(verify_rom_hash tools/verify_rom_hash/main.cpp)
    add_executable(verify_rom_hash_sha1_selftest tools/verify_rom_hash/sha1_selftest.cpp)
    add_test(NAME verify_rom_hash_sha1_vectors COMMAND verify_rom_hash_sha1_selftest)
    add_test(NAME verify_rom_hash_cli
        COMMAND ${CMAKE_COMMAND}
            -DTOOL=$<TARGET_FILE:verify_rom_hash>
            -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_rom_hash/test_verify.cmake)
endif()

# Examples (SDL2 + this library only).
if(GBAIMP_BUILD_EXAMPLES)
    enable_testing()
    add_executable(gbaimp_example_minimal examples/minimal_host/main.cpp)
    target_link_libraries(gbaimp_example_minimal PRIVATE gba_improvements)
    add_test(NAME example_minimal_headless
        COMMAND gbaimp_example_minimal --headless --frames 120)
    set_tests_properties(example_minimal_headless PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()

if(GBAIMP_BUILD_TESTS)
    # As a subproject, prefix target and test names so they cannot collide with
    # the parent's own. Note: ctest only sees these tests if the parent project
    # calls enable_testing() in its top-level CMakeLists.txt.
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(_gbaimp_test_prefix "")
    else()
        set(_gbaimp_test_prefix "gbaimp_")
    endif()
    enable_testing()
    set(_gbaimp_tests
        pixel_art_upscaler screen_filters audio_dsp hd_audio_player dialog_turbo
        hud_anchoring input_enhancements savestate_thumbnails hooks
        perf_hud frame_interpolator instances audio_pipeline sdl_interpose
        audio_device audio_threads game_host)
    foreach(_t IN LISTS _gbaimp_tests)
        add_executable(${_gbaimp_test_prefix}test_${_t} tests/test_${_t}.cpp)
        target_link_libraries(${_gbaimp_test_prefix}test_${_t} PRIVATE gba_improvements)
        # Tests define their own main(); stop SDL.h from renaming it to SDL_main (MSVC/MinGW).
        target_compile_definitions(${_gbaimp_test_prefix}test_${_t} PRIVATE SDL_MAIN_HANDLED)
        add_test(NAME ${_gbaimp_test_prefix}${_t} COMMAND ${_gbaimp_test_prefix}test_${_t})
        # No display or audio device is needed: SDL uses its dummy drivers.
        set_tests_properties(${_gbaimp_test_prefix}${_t} PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    endforeach()

    # sdl_interpose / audio_device: Linux exercises the --wrap shims, Windows the IAT patcher.
    foreach(_t sdl_interpose audio_device game_host)
        if(NOT WIN32)
            target_link_options(${_gbaimp_test_prefix}test_${_t} PRIVATE
                -Wl,--wrap=SDL_PollEvent -Wl,--wrap=SDL_UpdateTexture -Wl,--wrap=SDL_RenderCopy
                -Wl,--wrap=SDL_RenderPresent -Wl,--wrap=SDL_QueueAudio -Wl,--wrap=SDL_OpenAudioDevice)
        elseif(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
            add_custom_command(TARGET ${_gbaimp_test_prefix}test_${_t} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${_gbaimp_test_prefix}test_${_t}> $<TARGET_FILE_DIR:${_gbaimp_test_prefix}test_${_t}>
                COMMAND_EXPAND_LISTS)
        endif()
    endforeach()
    find_package(Threads REQUIRED)
    target_link_libraries(${_gbaimp_test_prefix}test_audio_device PRIVATE Threads::Threads)
    target_link_libraries(${_gbaimp_test_prefix}test_audio_threads PRIVATE Threads::Threads)
endif()
