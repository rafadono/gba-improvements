# Run with: cmake -DTOOL=<verify_rom_hash> -DWORKDIR=<dir> -P test_verify.cmake
file(WRITE "${WORKDIR}/verify_input.bin" "abc")   # 3 bytes, no newline
set(GOOD a9993e364706816aba3e25717850c26c9cd0d89d)

function(run_case name expect_code)
    execute_process(COMMAND "${TOOL}" ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL expect_code)
        message(FATAL_ERROR "${name}: expected exit ${expect_code}, got ${rc}\n${out}\n${err}")
    endif()
    message(STATUS "${name}: exit ${rc} (ok)")
endfunction()

string(TOUPPER ${GOOD} GOOD_UPPER)
run_case(match 0 "${WORKDIR}/verify_input.bin" ${GOOD})
run_case(match_uppercase 0 "${WORKDIR}/verify_input.bin" ${GOOD_UPPER})
run_case(mismatch 1 "${WORKDIR}/verify_input.bin" da39a3ee5e6b4b0d3255bfef95601890afd80709)
run_case(missing_file 2 "${WORKDIR}/does_not_exist.bin" ${GOOD})
run_case(bad_hash 2 "${WORKDIR}/verify_input.bin" nothex)
run_case(usage 2)
