cmake_minimum_required(VERSION 3.22)
project(Application LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_LIBC_LOCK_TEST "Run the two-thread libc lock self-test at startup" ON)
option(ENABLE_THREADSAFE_STATIC_TEST
       "Run the contended function-local static initialization self-test at startup"
       ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/Platform_Linux.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    set(TX_USER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Inc/tx_user.h)
    set(STM32_LINKER_SCRIPT
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/STM32H753XX_FLASH.ld)
    include(cmake/Platform_STM32.cmake)
else()
    message(FATAL_ERROR "Unsupported target system: ${CMAKE_SYSTEM_NAME}")
endif()

add_subdirectory(hal_compat)

add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        HalCompat
        Platform
)

target_compile_options(${PROJECT_NAME}
    PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(ENABLE_LIBC_LOCK_TEST)
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            ENABLE_LIBC_LOCK_TEST=1
    )
endif()

if(ENABLE_THREADSAFE_STATIC_TEST)
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            ENABLE_THREADSAFE_STATIC_TEST=1
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY
        LINK_DEPENDS ${STM32_LINKER_SCRIPT})

    target_link_options(${PROJECT_NAME}
        PRIVATE
            -T${STM32_LINKER_SCRIPT}
            --specs=nosys.specs
            -Wl,-Map=$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.map
            -Wl,--gc-sections
            -Wl,--print-memory-usage
    )

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex
                $<TARGET_FILE:${PROJECT_NAME}>
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary
                $<TARGET_FILE:${PROJECT_NAME}>
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
        COMMENT "Generating hex and bin files, and printing size"
        VERBATIM
    )
endif()
