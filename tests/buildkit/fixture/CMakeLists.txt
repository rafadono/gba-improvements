# Minimal host used by the build-kit test: calls gbaimp_add_host() against stubs
# (no gbarecomp, no recomp-ui, no SDL2) and checks the properties it sets.
#   -DFIXTURE_PLATFORM=native|msvc|gnu   which platform branch to take
#   -DFIXTURE_RUI=ON|OFF                 recomp-ui present
#   -DFIXTURE_STACK_COMMAND=ON|OFF       recomp-ui provides target_link_gbarecomp_runtime_stack
#   -DFIXTURE_GENERATED=ON|OFF           a generated directory with dispatch_table.cpp exists
cmake_minimum_required(VERSION 3.20)
project(FixtureHost C CXX)

set(FIXTURE_PLATFORM "native" CACHE STRING "")
option(FIXTURE_RUI "" ON)
option(FIXTURE_STACK_COMMAND "" OFF)
option(FIXTURE_GENERATED "" OFF)

# Force a platform branch regardless of the real compiler (configure-only checks).
if(FIXTURE_PLATFORM STREQUAL "msvc")
    set(MSVC TRUE)
elseif(FIXTURE_PLATFORM STREQUAL "gnu")
    set(MSVC FALSE)
endif()

set(GBARECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp")
add_subdirectory("${GBARECOMP_ROOT}" gbarecomp_build EXCLUDE_FROM_ALL)

set(RECOMP_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/recomp-ui")
if(FIXTURE_RUI)
    include("${RECOMP_UI_DIR}/recomp_ui.cmake")
else()
    set(RECOMP_UI_DIR "")
endif()

add_library(gba_improvements INTERFACE)
add_library(gbaimp::gba_improvements ALIAS gba_improvements)

include("${GBAIMP_HOST_CMAKE}")

set(_gen_dir "${CMAKE_CURRENT_SOURCE_DIR}/generated")
if(FIXTURE_GENERATED)
    set(_gen_dir "${CMAKE_CURRENT_SOURCE_DIR}/generated_present")
endif()

gbaimp_add_host(FixtureGame
    SYMBOL_PREFIX fix
    SOURCES src/main.cpp src/game.cpp
    TEST_TARGET fixture_unit_tests
    TEST_SOURCES tests/test_all.cpp src/game.cpp src/scaffold_stub.cpp
    GENERATED_DIR "${_gen_dir}"
    SCAFFOLD_STUB src/scaffold_stub.cpp)

# ---------------------------------------------------------------- assertions
function(_fail what)
    message(FATAL_ERROR "FIXTURE CHECK FAILED: ${what}")
endfunction()

# _in(<what> <list variable> <item> <TRUE|FALSE>): item must (not) be in the list.
function(_in what list_var item want)
    list(FIND ${list_var} "${item}" _idx)
    if(_idx EQUAL -1)
        set(_present FALSE)
    else()
        set(_present TRUE)
    endif()
    if(NOT _present STREQUAL want)
        _fail("${what} (item '${item}', expected present=${want})")
    endif()
endfunction()

function(_eq what actual expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        _fail("${what}: got '${actual}', expected '${expected}'")
    endif()
endfunction()

function(_prop tgt name out)
    get_target_property(_v ${tgt} ${name})
    if("${_v}" MATCHES "NOTFOUND$")
        set(_v "")
    endif()
    set(${out} "${_v}" PARENT_SCOPE)
endfunction()

_prop(FixtureGame SOURCES exe_src)
_prop(FixtureGame LINK_OPTIONS exe_lo)
_prop(FixtureGame LINK_LIBRARIES exe_ll)
_prop(FixtureGame COMPILE_DEFINITIONS exe_cd)
_prop(FixtureGame CXX_SCAN_FOR_MODULES exe_scan)
_prop(fixture_unit_tests SOURCES t_src)
_prop(fixture_unit_tests LINK_OPTIONS t_lo)
_prop(fixture_unit_tests LINK_LIBRARIES t_ll)
_prop(fixture_unit_tests COMPILE_DEFINITIONS t_cd)
_prop(fixture_unit_tests CXX_SCAN_FOR_MODULES t_scan)

_eq("exe CXX_SCAN_FOR_MODULES" "${exe_scan}" "OFF")
_eq("test CXX_SCAN_FOR_MODULES" "${t_scan}" "OFF")
_in("exe links the library" exe_ll "gbaimp::gba_improvements" TRUE)
_in("tests link the library" t_ll "gbaimp::gba_improvements" TRUE)

# generated sources vs scaffold stub (exe only)
if(FIXTURE_GENERATED)
    set(_gp "${CMAKE_CURRENT_SOURCE_DIR}/generated_present")
    _in("generated dispatch table compiled" exe_src "${_gp}/dispatch_table.cpp" TRUE)
    _in("generated C source compiled" exe_src "${_gp}/gen.c" TRUE)
    _in("no scaffold stub when generated" exe_src "src/scaffold_stub.cpp" FALSE)
    get_source_file_property(_gw "${_gp}/gen.c" COMPILE_OPTIONS)
    if(MSVC)
        _eq("generated sources compiled with /w" "${_gw}" "/w")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        _eq("generated sources compiled with -w" "${_gw}" "-w")
    endif()
else()
    _in("scaffold stub used without generated code" exe_src "src/scaffold_stub.cpp" TRUE)
endif()

# recomp-ui launcher swap
if(FIXTURE_RUI)
    foreach(_s exe_src t_src)
        set(_has_stock FALSE)
        foreach(_f IN LISTS ${_s})
            if(_f MATCHES "runtime_ui_imgui[.]cpp$")
                set(_has_stock TRUE)
            endif()
        endforeach()
        _eq("${_s}: stock runtime_ui_imgui.cpp removed" "${_has_stock}" "FALSE")
        # The shared renderer comes from the library (optional/recomp_ui), not from the host.
        get_filename_component(_lib_root "${GBAIMP_HOST_CMAKE}/../.." ABSOLUTE)
        set(_shared "${_lib_root}/optional/recomp_ui/runtime_ui_imgui_enhanced.cpp")
        if(NOT EXISTS "${_shared}")
            _fail("shared enhanced imgui source missing: ${_shared}")
        endif()
        _in("${_s}: shared enhanced imgui source added" ${_s} "${_shared}" TRUE)
        _in("${_s}: no host-local enhanced source" ${_s} "src/runtime_ui_imgui_enhanced.cpp" FALSE)
    endforeach()
endif()

if(NOT FIXTURE_RUI)
    foreach(_s exe_src t_src)
        foreach(_f IN LISTS ${_s})
            if(_f MATCHES "runtime_ui_imgui")
                _fail("${_s}: imgui source compiled without recomp-ui: ${_f}")
            endif()
        endforeach()
    endforeach()
endif()

# SDL wraps (GNU ld) vs none (MSVC)
set(_wrap_rui "-Wl,--wrap=recomp_runtime_ui_create_standard")
if(MSVC)
    _eq("MSVC: no link options on exe" "${exe_lo}" "")
    _eq("MSVC: no link options on tests" "${t_lo}" "")
else()
    foreach(_w SDL_PollEvent SDL_RenderPresent SDL_UpdateTexture SDL_RenderCopy SDL_QueueAudio SDL_OpenAudioDevice)
        _in("exe wraps ${_w}" exe_lo "-Wl,--wrap=${_w}" TRUE)
        _in("tests wrap ${_w}" t_lo "-Wl,--wrap=${_w}" TRUE)
    endforeach()
    _in("exe wraps recomp_runtime_ui_create_standard" exe_lo "${_wrap_rui}" TRUE)
    _in("tests wrap recomp_runtime_ui_create_standard" t_lo "${_wrap_rui}" TRUE)
endif()

# MSVC rename of the recomp-ui symbol
set(_ren "")
if(FIXTURE_RUI)
    get_source_file_property(_ren "${RECOMP_UI_DIR}/src/common/recomp_runtime_settings.c" COMPILE_DEFINITIONS)
    if("${_ren}" MATCHES "NOTFOUND$")
        set(_ren "")
    endif()
endif()
if(MSVC AND FIXTURE_RUI)
    _in("exe defines the rename macro" exe_cd "FIX_RENAME_RUNTIME_UI_CREATE_STANDARD=1" TRUE)
    _in("tests define the rename macro" t_cd "FIX_RENAME_RUNTIME_UI_CREATE_STANDARD=1" TRUE)
    _eq("settings.c rename definition" "${_ren}"
        "recomp_runtime_ui_create_standard=fix_real_recomp_runtime_ui_create_standard")
else()
    _in("no rename macro on exe outside MSVC+recomp-ui" exe_cd "FIX_RENAME_RUNTIME_UI_CREATE_STANDARD=1" FALSE)
    _in("no rename macro on tests outside MSVC+recomp-ui" t_cd "FIX_RENAME_RUNTIME_UI_CREATE_STANDARD=1" FALSE)
    _eq("settings.c untouched outside MSVC+recomp-ui" "${_ren}" "")
endif()

# runtime stack linking
if(FIXTURE_RUI AND FIXTURE_STACK_COMMAND)
    _in("stack command used for exe" exe_ll "gbarecomp_gba" TRUE)
    _in("direct libs not linked when the stack command exists" exe_ll "gbarecomp_armv4t" FALSE)
else()
    _in("gbarecomp libs linked directly (exe)" exe_ll "gbarecomp_armv4t" TRUE)
    _in("gbarecomp libs linked directly (tests)" t_ll "gbarecomp_armv4t" TRUE)
    if(NOT MSVC AND NOT APPLE)
        _in("exe uses --start-group on GNU ld" exe_ll "-Wl,--start-group" TRUE)
        _in("tests link the libs without --start-group" t_ll "-Wl,--start-group" FALSE)
    endif()
endif()
if(UNIX AND NOT APPLE)
    _in("pthread linked" exe_ll "pthread" TRUE)
endif()

# Include directories
get_target_property(_inc FixtureGame INCLUDE_DIRECTORIES)
get_target_property(_tinc fixture_unit_tests INCLUDE_DIRECTORIES)
_in("exe includes gbarecomp armv4t" _inc "${GBARECOMP_ROOT}/src/armv4t" TRUE)
_in("tests do not include armv4t" _tinc "${GBARECOMP_ROOT}/src/armv4t" FALSE)

message(STATUS "FIXTURE: all property checks passed (platform=${FIXTURE_PLATFORM}, MSVC=${MSVC})")
