add_library(hal_drivers STATIC)
add_library(hal::drivers ALIAS hal_drivers)

target_include_directories(hal_drivers
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hal_drivers
    PUBLIC
        platform
        project_compiler_settings
)
