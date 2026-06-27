add_library(hal STATIC)

target_link_libraries(hal
    PUBLIC
        platform
        project_compiler_settings
)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    add_subdirectory(stm32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(linux)
else()
    message(FATAL_ERROR "Unsupported HAL platform: ${CMAKE_SYSTEM_NAME}")
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        hal
)
