cmake_minimum_required(VERSION 4.5)

function(check_rejection)
    execute_process(COMMAND "${RENDERER}" ${ARGN}
        RESULT_VARIABLE status ERROR_VARIABLE diagnostic)
    string(REPLACE "\\" "/" diagnostic "${diagnostic}")
    set(expected "${MODEL}: Mesh 1 primitive 2: indices[4] = 7 is out of range for 3 POSITION vertices")
    string(FIND "${diagnostic}" "${expected}" match)
    if(NOT status STREQUAL "1" OR match EQUAL -1)
        message(FATAL_ERROR "${ARGN} returned ${status}. Expected '${expected}', got '${diagnostic}'")
    endif()
endfunction()

check_rejection(--inspect "${MODEL}")
check_rejection(--render "${MODEL}" --pixel 0 0)
