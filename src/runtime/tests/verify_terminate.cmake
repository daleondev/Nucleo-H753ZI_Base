if(NOT DEFINED PROBE)
    message(FATAL_ERROR "PROBE is required")
endif()

set(probe_command "${PROBE}")
if(DEFINED PROBE_MODE)
    list(APPEND probe_command "${PROBE_MODE}")
endif()

execute_process(
    COMMAND ${probe_command}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 5
)

if(result EQUAL 0)
    message(FATAL_ERROR "Terminate probe returned normally")
endif()

if(result MATCHES "[Tt]imeout")
    message(FATAL_ERROR "Terminate probe timed out")
endif()

string(FIND "${error}" "[sim][hal] Error_Handler" error_handler_position)
if(error_handler_position EQUAL -1)
    message(FATAL_ERROR
        "Terminate probe did not reach Error_Handler\nstdout:\n${output}\nstderr:\n${error}"
    )
endif()
