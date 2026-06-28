add_subdirectory(drivers)

add_library(hal STATIC)

target_sources(hal
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/hal.cpp
)

target_link_libraries(hal
    PUBLIC
        hal::drivers
        platform
        project_compiler_settings
)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    target_compile_definitions(hal
        PUBLIC
            HAL_PLATFORM_STM32
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(hal
        PUBLIC
            HAL_PLATFORM_LINUX
    )
else()
    message(FATAL_ERROR "Unsupported HAL platform: ${CMAKE_SYSTEM_NAME}")
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        hal
)
