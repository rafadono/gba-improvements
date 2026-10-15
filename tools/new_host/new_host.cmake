# new_host.cmake: instantiate the host templates in templates/host/ for a new game.
#
#   cmake -DOUT_DIR=<host repo> -DGAME_EXE=MyGameRecomp -DSYMBOL_PREFIX=mygame \
#         -DROM_FILE="My Game (USA).gba" -DRECOMP_CONFIG=mygame.toml -P tools/new_host/new_host.cmake
#
# Required: OUT_DIR, GAME_EXE, SYMBOL_PREFIX, ROM_FILE, RECOMP_CONFIG
# Optional: TEST_TARGET (default <SYMBOL_PREFIX lowercase>_unit_tests)
#           PROJECT_NAME (default GAME_EXE)
#           FORCE=ON     overwrite files that already exist (default: stop instead)
#           TEMPLATE_DIR (default ../../templates/host relative to this script)
#
# Placeholders in the templates (@NAME@):
#   GAME_EXE            executable target and file name, also the log tag of the scripts
#   PROJECT_NAME        CMake project() name
#   SYMBOL_PREFIX       prefix for the MSVC recomp-ui rename (lowercase form is used as given)
#   SYMBOL_PREFIX_UPPER SYMBOL_PREFIX in uppercase (CMake variable names)
#   TEST_TARGET         native unit-test executable
#   ROM_FILE            default ROM file name inside roms/ (build.ps1 -RomPath default)
#   RECOMP_CONFIG       gbarecomp config file name inside config/, WITH its extension (for example mygame.toml).
#                       In Windows PowerShell 5.1 quote the whole argument, "-DRECOMP_CONFIG=mygame.toml": unquoted,
#                       the argument is split at the dot and the name loses its extension.
#
# The script fails if any @PLACEHOLDER@ is left unresolved in an output file.

cmake_minimum_required(VERSION 3.16)

foreach(_req OUT_DIR GAME_EXE SYMBOL_PREFIX ROM_FILE RECOMP_CONFIG)
    if(NOT DEFINED ${_req} OR "${${_req}}" STREQUAL "")
        message(FATAL_ERROR "new_host: -D${_req}=... is required")
    endif()
endforeach()
if(NOT PROJECT_NAME)
    set(PROJECT_NAME "${GAME_EXE}")
endif()
foreach(_id GAME_EXE SYMBOL_PREFIX PROJECT_NAME)
    if(NOT "${${_id}}" MATCHES "^[A-Za-z][A-Za-z0-9_]*$")
        message(FATAL_ERROR "new_host: ${_id}='${${_id}}' must be a C identifier-like name ([A-Za-z][A-Za-z0-9_]*)")
    endif()
endforeach()
string(TOUPPER "${SYMBOL_PREFIX}" SYMBOL_PREFIX_UPPER)
string(TOLOWER "${SYMBOL_PREFIX}" _prefix_lower)
if(NOT TEST_TARGET)
    set(TEST_TARGET "${_prefix_lower}_unit_tests")
endif()
if(NOT TEMPLATE_DIR)
    get_filename_component(TEMPLATE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../templates/host" ABSOLUTE)
endif()

# template (relative to TEMPLATE_DIR) -> output (relative to OUT_DIR)
set(_map
    "CMakeLists.txt.in|CMakeLists.txt"
    "build.ps1.in|build.ps1"
    "build.sh.in|build.sh"
    "docker/Dockerfile.in|docker/Dockerfile"
    "github/workflows/ci.yml.in|.github/workflows/ci.yml")
set(_vars GAME_EXE PROJECT_NAME SYMBOL_PREFIX SYMBOL_PREFIX_UPPER TEST_TARGET ROM_FILE RECOMP_CONFIG)

# First pass: render everything and check, so nothing is written on error.
set(_outputs "")
foreach(_entry IN LISTS _map)
    string(REPLACE "|" ";" _pair "${_entry}")
    list(GET _pair 0 _in)
    list(GET _pair 1 _out)
    if(NOT EXISTS "${TEMPLATE_DIR}/${_in}")
        message(FATAL_ERROR "new_host: template not found: ${TEMPLATE_DIR}/${_in}")
    endif()
    if(EXISTS "${OUT_DIR}/${_out}" AND NOT FORCE)
        message(FATAL_ERROR "new_host: ${OUT_DIR}/${_out} already exists (pass -DFORCE=ON to overwrite)")
    endif()
    file(READ "${TEMPLATE_DIR}/${_in}" _text)
    foreach(_v IN LISTS _vars)
        string(REPLACE "@${_v}@" "${${_v}}" _text "${_text}")
    endforeach()
    if(_text MATCHES "@[A-Za-z][A-Za-z0-9_]*@")
        string(REGEX MATCHALL "@[A-Za-z][A-Za-z0-9_]*@" _left "${_text}")
        list(REMOVE_DUPLICATES _left)
        message(FATAL_ERROR "new_host: unresolved placeholders in ${_in}: ${_left}")
    endif()
    set(_rendered_${_out} "${_text}")
    list(APPEND _outputs "${_out}")
endforeach()

foreach(_out IN LISTS _outputs)
    # LF everywhere: the generated scripts run on Linux and the repositories keep LF in the index, even when
    # the template files were checked out with CRLF on Windows.
    string(REPLACE "
" "
" _lf_text "${_rendered_${_out}}")
    file(WRITE "${OUT_DIR}/${_out}" "${_lf_text}")
    message(STATUS "new_host: wrote ${OUT_DIR}/${_out}")
endforeach()
