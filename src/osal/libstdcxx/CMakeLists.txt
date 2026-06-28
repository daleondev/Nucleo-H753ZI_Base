add_library(osal_libstdcxx STATIC)
add_library(osal::libstdcxx ALIAS osal_libstdcxx)

target_sources(osal_libstdcxx
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/backend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/libstdcxx.cpp
)

target_include_directories(osal_libstdcxx
    BEFORE PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/overlay
)

target_link_libraries(osal_libstdcxx
    PRIVATE
        hal
    PUBLIC
        threadx
        project_compiler_settings
)

target_compile_definitions(osal_libstdcxx
    PUBLIC
        THREADX_STD_ENABLED=1
        _GLIBCXX_HAS_GTHREADS=1
        _GLIBCXX_HAVE_POSIX_SEMAPHORE=1
        _GLIBCXX_USE_SCHED_YIELD=1
)

# The ThreadX startup extension is the only direct reference to this runtime
# hook. Keep its archive member even when the application does not use a
# libstdc++ synchronization type directly.
target_link_options(osal_libstdcxx
    INTERFACE
        LINKER:--undefined=osal_libstdcxx_initialize
)
