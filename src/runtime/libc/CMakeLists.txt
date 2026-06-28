add_library(runtime_libc STATIC)
add_library(runtime::libc ALIAS runtime_libc)

target_sources(runtime_libc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libc.c
)

target_link_libraries(runtime_libc
    PRIVATE
        hal
    PUBLIC
        threadx
)

target_include_directories(runtime_libc
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_features(runtime_libc
    PUBLIC
        c_std_11
)

target_compile_options(runtime_libc
    PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    # Runtime hook providers have no direct application reference. Force this
    # archive member into the link so ThreadX and libc can resolve the hooks.
    target_link_options(runtime_libc
        INTERFACE
            LINKER:--undefined=runtime_libc_initialize
    )
endif()
