add_library(runtime INTERFACE)
add_library(runtime::runtime ALIAS runtime)

add_subdirectory(libc)
add_subdirectory(libstdcxx)

target_sources(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/threadx/startup.cpp
)

target_link_libraries(runtime
    INTERFACE
        runtime::libc
        runtime::libstdcxx
        project_compiler_settings
)

target_link_options(runtime
    INTERFACE
        LINKER:--wrap=main
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        runtime
)
