set(THREADX_ARCH linux)
set(THREADX_TOOLCHAIN gnu)
add_subdirectory(${PROJECT_SOURCE_DIR}/external/threadx)

target_compile_definitions(threadx PUBLIC TX_ENABLE_STACK_CHECKING TX_LINUX_MULTI_CORE)

find_package(Threads REQUIRED)

add_library(Platform INTERFACE)

target_link_libraries(Platform
    INTERFACE
        threadx
        Threads::Threads
        rt
)

target_compile_definitions(Platform
    INTERFACE
        $<$<CONFIG:Debug>:DEBUG>
)
