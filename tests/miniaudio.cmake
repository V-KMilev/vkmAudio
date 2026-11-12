# Configures tests/miniaudio/ with a miniaudio of the consumer's and without one (cmake -P):
#   -DLIBRARY_DIR=<root> -DOUT=<folder> -DGENERATOR=<generator>

file(REMOVE_RECURSE "${OUT}")
foreach(_own ON OFF)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}/miniaudio" -B "${OUT}/${_own}"
                -G "${GENERATOR}" "-DVKM_LIBRARY_DIR=${LIBRARY_DIR}" "-DVKM_CONSUMER_MINIAUDIO=${_own}"
        RESULT_VARIABLE _status
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE  _output
    )
    if(NOT _status EQUAL 0)
        message(FATAL_ERROR "With the consumer's own miniaudio ${_own}:\n${_output}")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUT}")
