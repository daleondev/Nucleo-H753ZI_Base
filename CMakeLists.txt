cmake_minimum_required(VERSION 3.22)
project(Application LANGUAGES C CXX ASM)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
    message(FATAL_ERROR "This project requires a GNU C++ compiler with C++26 support (GCC 15+)")
endif()

add_library(project_compiler_settings INTERFACE)
target_compile_options(project_compiler_settings
    INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-std=c++26>
)

option(ENABLE_LIBC_LOCK_TEST "Run the two-thread libc lock self-test at startup" ON)
option(ENABLE_THREADSAFE_STATIC_TEST
       "Run the contended function-local static initialization self-test at startup"
       ON)
option(ENABLE_STANDARD_LIBRARY_SELF_TEST
       "Run the ThreadX-backed C++ concurrency self-test at startup"
       OFF)
option(ENABLE_CLANG_TIDY "Run clang-tidy while compiling project-owned C++ targets" OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/platform_linux.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    include(cmake/platform_stm32.cmake)
else()
    message(FATAL_ERROR "Unsupported target system: ${CMAKE_SYSTEM_NAME}")
endif()

add_executable(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        platform
        project_compiler_settings
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

if(ENABLE_STANDARD_LIBRARY_SELF_TEST)
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            ENABLE_STANDARD_LIBRARY_SELF_TEST=1
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY
        LINK_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/STM32H753XX_FLASH.ld
    )

    target_link_options(${PROJECT_NAME}
        PRIVATE
            -T${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/STM32H753XX_FLASH.ld
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

add_subdirectory(src)

if(ENABLE_CLANG_TIDY)
    # LLVM 18 cannot parse GCC 15's C++26 <format> implementation.
    find_program(CLANG_TIDY_20_EXECUTABLE NAMES clang-tidy-20 REQUIRED)
    set_target_properties(${PROJECT_NAME} hal runtime_libstdcxx PROPERTIES
        CXX_CLANG_TIDY "${CLANG_TIDY_20_EXECUTABLE}"
    )
endif()
