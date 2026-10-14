# Driver for the build-kit test (cmake -P). Copies the fixture host to WORK, configures it for
# several platform / recomp-ui / generated-code variants (the fixture asserts the properties
# gbaimp_add_host() sets), then builds the native variant and checks the post-build copies.
#
# Inputs: -DFIXTURE=<dir> -DWORK=<dir> -DHOST_CMAKE=<GbaimpHost.cmake> -DGEN=<generator>
#         [-DPLATFORM_ARG=<-A value>] [-DMAKE_PROGRAM=<path>] [-DC_COMPILER=.. -DCXX_COMPILER=..]

foreach(_r FIXTURE WORK HOST_CMAKE GEN)
    if(NOT ${_r})
        message(FATAL_ERROR "run_buildkit_tests: -D${_r} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(_gen_args -G "${GEN}")
if(PLATFORM_ARG)
    list(APPEND _gen_args -A "${PLATFORM_ARG}")
endif()
if(MAKE_PROGRAM)
    list(APPEND _gen_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
list(APPEND _gen_args -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1)
if(C_COMPILER)
    list(APPEND _gen_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(CXX_COMPILER)
    list(APPEND _gen_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()

# Each variant gets its own copy of the fixture so post-build copies never touch the repository.
function(_variant name)
    file(COPY "${FIXTURE}/" DESTINATION "${WORK}/s_${name}")
    file(MAKE_DIRECTORY "${WORK}/${name}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${WORK}/s_${name}" -B "${WORK}/${name}" ${_gen_args}
            "-DGBAIMP_HOST_CMAKE=${HOST_CMAKE}" ${ARGN}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "configure '${name}' failed (${_rc}):\n${_out}\n${_err}")
    endif()
    if(NOT _out MATCHES "FIXTURE: all property checks passed")
        message(FATAL_ERROR "configure '${name}': fixture assertions did not run:\n${_out}")
    endif()
    message(STATUS "variant ${name}: ok")
endfunction()

_variant(g1 -DFIXTURE_PLATFORM=gnu  -DFIXTURE_RUI=ON  -DFIXTURE_STACK_COMMAND=ON  -DFIXTURE_GENERATED=ON)
_variant(g2   -DFIXTURE_PLATFORM=gnu  -DFIXTURE_RUI=ON  -DFIXTURE_STACK_COMMAND=OFF)
_variant(g3     -DFIXTURE_PLATFORM=gnu  -DFIXTURE_RUI=OFF)
_variant(m1      -DFIXTURE_PLATFORM=msvc -DFIXTURE_RUI=ON  -DFIXTURE_STACK_COMMAND=OFF)
_variant(m2  -DFIXTURE_PLATFORM=msvc -DFIXTURE_RUI=ON  -DFIXTURE_STACK_COMMAND=ON  -DFIXTURE_GENERATED=ON)
_variant(m3    -DFIXTURE_PLATFORM=msvc -DFIXTURE_RUI=OFF)

# Native variant: also build, to exercise the post-build copy to the source root.
_variant(nat -DFIXTURE_PLATFORM=native -DFIXTURE_RUI=OFF)
set(_src "${WORK}/s_nat")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${WORK}/nat" --config Release --target FixtureGame
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "build of the native fixture failed (${_rc}):\n${_out}\n${_err}")
endif()
set(_suffix "")
if(WIN32)
    set(_suffix ".exe")
endif()
if(NOT EXISTS "${_src}/FixtureGame${_suffix}")
    message(FATAL_ERROR "post-build did not copy the executable to the source root (${_src}/FixtureGame${_suffix})")
endif()
if(NOT EXISTS "${_src}/SDL2.dll")
    message(FATAL_ERROR "post-build did not copy SDL2.dll to the source root")
endif()
file(GLOB_RECURSE _dll_next_to_exe "${WORK}/nat/SDL2.dll")
if(NOT _dll_next_to_exe)
    message(FATAL_ERROR "post-build did not copy SDL2.dll next to the built executable")
endif()
message(STATUS "build kit test: all variants passed")
