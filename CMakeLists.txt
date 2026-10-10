cmake_minimum_required(VERSION 3.16)
project(gba_improvements CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

# Unit tests: on by default only when this is the top-level project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_gbaimp_tests_default ON)
else()
    set(_gbaimp_tests_default OFF)
endif()
option(GBAIMP_BUILD_TESTS "Build the gbaimp unit tests" ${_gbaimp_tests_default})

add_library(gba_improvements STATIC
    src/hooks.cpp
    src/xbrz_filter.cpp
    src/screen_filters.cpp
    src/frame_interpolator.cpp
    src/audio_dsp.cpp
    src/hd_audio_player.cpp
    src/dialog_turbo.cpp
    src/savestate_thumbnails.cpp
    src/perf_hud.cpp
    src/input_enhancements.cpp
    src/hud_anchoring.cpp)
add_library(gbaimp::gba_improvements ALIAS gba_improvements)

target_include_directories(gba_improvements PUBLIC include)
if(TARGET SDL2::SDL2)
    target_link_libraries(gba_improvements PUBLIC SDL2::SDL2)
else()
    target_include_directories(gba_improvements PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(gba_improvements PUBLIC ${SDL2_LIBRARIES})
endif()

# Optional: CLI helper that checks a ROM against an expected SHA-1.
# Needs gba/sha1.h from the host project's gbarecomp checkout.
if(GBAIMP_GBARECOMP_INCLUDE)
    add_executable(verify_rom_hash tools/verify_rom_hash/main.cpp)
    target_include_directories(verify_rom_hash PRIVATE ${GBAIMP_GBARECOMP_INCLUDE})
endif()

if(GBAIMP_BUILD_TESTS)
    enable_testing()
    set(_gbaimp_tests
        xbrz screen_filters audio_dsp hd_audio_player dialog_turbo
        hud_anchoring input_enhancements savestate_thumbnails hooks)
    foreach(_t IN LISTS _gbaimp_tests)
        add_executable(test_${_t} tests/test_${_t}.cpp)
        target_link_libraries(test_${_t} PRIVATE gba_improvements)
        add_test(NAME ${_t} COMMAND test_${_t})
    endforeach()
endif()
