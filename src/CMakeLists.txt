if(RUNTIME_HARDWARE_SELF_TEST)
    target_sources(${PROJECT_NAME}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/runtime/tests/hardware_self_test.cpp
    )
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            RUNTIME_APPLICATION_THREAD_STACK_SIZE=32768U
    )
else()
    target_sources(${PROJECT_NAME}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    )
endif()

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(RUNTIME_HARDWARE_SELF_TEST)
    target_compile_definitions(runtime_libstdcxx
        PRIVATE
            RUNTIME_STD_THREAD_STACK_SIZE=8192U
    )
endif()

if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(hal/tests)
endif()
