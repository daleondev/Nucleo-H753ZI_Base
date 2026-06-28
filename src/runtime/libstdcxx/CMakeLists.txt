add_library(runtime_libstdcxx STATIC)
add_library(runtime::libstdcxx ALIAS runtime_libstdcxx)

target_sources(runtime_libstdcxx
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/backend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/cxx_guard.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/libstdcxx.cpp
)

target_include_directories(runtime_libstdcxx
    BEFORE PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/overlay
)

target_link_libraries(runtime_libstdcxx
    PRIVATE
        hal
    PUBLIC
        threadx
        project_compiler_settings
)

target_compile_definitions(runtime_libstdcxx
    PUBLIC
        THREADX_STD_ENABLED=1
        _GLIBCXX_HAS_GTHREADS=1
        _GLIBCXX_HAVE_POSIX_SEMAPHORE=1
        _GLIBCXX_USE_SCHED_YIELD=1
)

# Keep the automatic runtime hook and the replacement guard ABI in the final
# link even when no ordinary C++ reference pulls their archive members first.
target_link_options(runtime_libstdcxx
    INTERFACE
        LINKER:--undefined=__cxa_guard_acquire
        LINKER:--undefined=runtime_libstdcxx_initialize
)
