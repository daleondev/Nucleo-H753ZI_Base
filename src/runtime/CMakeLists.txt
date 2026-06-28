add_library(runtime INTERFACE)
add_library(runtime::runtime ALIAS runtime)

add_subdirectory(libc)
add_subdirectory(libstdcxx)

target_link_libraries(runtime
    INTERFACE
        runtime::libc
        runtime::libstdcxx
        project_compiler_settings
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        runtime
)
