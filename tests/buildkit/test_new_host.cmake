# Test for tools/new_host/new_host.cmake (cmake -P).
# Inputs: -DTOOL=<new_host.cmake> -DWORK=<scratch dir> -DTEMPLATES=<templates/host>

foreach(_r TOOL WORK TEMPLATES)
    if(NOT ${_r})
        message(FATAL_ERROR "test_new_host: -D${_r} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(_run out_rc out_text)
    execute_process(COMMAND "${CMAKE_COMMAND}" ${ARGN} -P "${TOOL}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_VARIABLE _e)
    set(${out_rc} ${_rc} PARENT_SCOPE)
    set(${out_text} "${_o}${_e}" PARENT_SCOPE)
endfunction()

function(_fail what)
    message(FATAL_ERROR "test_new_host FAILED: ${what}")
endfunction()

function(_ok rc what)
    if(NOT rc EQUAL 0)
        _fail("${what}")
    endif()
endfunction()

function(_nonzero rc what)
    if(rc EQUAL 0)
        _fail("${what}")
    endif()
endfunction()

function(_has text regex what)
    if(NOT "${text}" MATCHES "${regex}")
        _fail("${what} (pattern: ${regex})")
    endif()
endfunction()

function(_lacks text regex what)
    if("${text}" MATCHES "${regex}")
        _fail("${what} (pattern: ${regex})")
    endif()
endfunction()

function(_exists path want what)
    if(want AND NOT EXISTS "${path}")
        _fail("${what}: ${path} missing")
    elseif(NOT want AND EXISTS "${path}")
        _fail("${what}: ${path} should not exist")
    endif()
endfunction()

set(_args -DOUT_DIR=${WORK}/host -DGAME_EXE=MyGameRecomp -DSYMBOL_PREFIX=mygame
    "-DROM_FILE=My Game (USA).gba" -DRECOMP_CONFIG=mygame.toml)

# 1. Happy path: every output exists and has no unresolved placeholder.
_run(rc text ${_args})
_ok(${rc} "instantiation succeeds: ${text}")
foreach(_f CMakeLists.txt build.ps1 build.sh docker/Dockerfile .github/workflows/ci.yml)
    _exists("${WORK}/host/${_f}" TRUE "${_f} was written")
    file(READ "${WORK}/host/${_f}" _content)
    _lacks("${_content}" "@[A-Za-z][A-Za-z0-9_]*@" "${_f} has no unresolved placeholder")
endforeach()

# 2. Values landed where expected.
file(READ "${WORK}/host/CMakeLists.txt" _cm)
_has("${_cm}" "project[(]MyGameRecomp C CXX[)]" "project() name")
_has("${_cm}" "gbaimp_add_host[(]MyGameRecomp" "gbaimp_add_host call")
_has("${_cm}" "SYMBOL_PREFIX mygame" "symbol prefix")
_has("${_cm}" "TEST_TARGET mygame_unit_tests" "default test target derived from the prefix")
_has("${_cm}" "MYGAME_COMMON_SOURCES" "uppercase prefix variable")
file(READ "${WORK}/host/build.ps1" _ps)
_has("${_ps}" "roms.My Game [(]USA[)][.]gba" "ROM default in build.ps1")
_has("${_ps}" "config.mygame[.]toml" "recompiler config in build.ps1")
_has("${_ps}" "function Invoke-Checked" "build.ps1 checks native exit codes")
_has("${_ps}" "exit [$]code" "build.ps1 propagates the exit code")
file(READ "${WORK}/host/build.sh" _sh)
_has("${_sh}" "config/mygame[.]toml" "recompiler config in build.sh")
string(FIND "${_sh}" "\r" _cr)
if(NOT _cr EQUAL -1)
    _fail("build.sh must use LF line endings")
endif()
file(READ "${WORK}/host/.github/workflows/ci.yml" _ci)
_has("${_ci}" "mygame_unit_tests" "test target in ci.yml")

# 3. Refuses to overwrite, unless FORCE.
_run(rc text ${_args})
_nonzero(${rc} "second run without FORCE must fail")
_run(rc text ${_args} -DFORCE=ON)
_ok(${rc} "second run with FORCE succeeds: ${text}")

# 4. Argument validation.
_run(rc text -DOUT_DIR=${WORK}/bad1 -DGAME_EXE=X -DSYMBOL_PREFIX=x)
_nonzero(${rc} "missing ROM_FILE / RECOMP_CONFIG must fail")
_run(rc text -DOUT_DIR=${WORK}/bad2 -DGAME_EXE=bad-name -DSYMBOL_PREFIX=x -DROM_FILE=a.gba -DRECOMP_CONFIG=a.toml)
_nonzero(${rc} "invalid GAME_EXE must fail")
_exists("${WORK}/bad2" FALSE "nothing is written on invalid input")

# 5. A template with an unknown placeholder is rejected and nothing is written.
file(COPY "${TEMPLATES}/" DESTINATION "${WORK}/tpl")
file(APPEND "${WORK}/tpl/build.sh.in" "\necho @NOT_A_REAL_PLACEHOLDER@\n")
_run(rc text -DOUT_DIR=${WORK}/bad3 -DGAME_EXE=Y -DSYMBOL_PREFIX=y -DROM_FILE=a.gba -DRECOMP_CONFIG=a.toml
    -DTEMPLATE_DIR=${WORK}/tpl)
_nonzero(${rc} "unresolved placeholder must fail")
_has("${text}" "NOT_A_REAL_PLACEHOLDER" "the error names the placeholder")
_exists("${WORK}/bad3/build.sh" FALSE "nothing is written when a placeholder is unresolved")

message(STATUS "test_new_host: all checks passed")
