add_library(osal_libc STATIC)
add_library(osal::libc ALIAS osal_libc)

target_sources(osal_libc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libc.c
)

target_link_libraries(osal_libc
    PRIVATE
        hal
    PUBLIC
        threadx
)

target_include_directories(osal_libc
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_features(osal_libc
    PUBLIC
        c_std_11
)

target_compile_options(osal_libc
    PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    # Runtime hook providers have no direct application reference. Force this
    # archive member into the link so ThreadX and Newlib can resolve the hooks.
    target_link_options(osal_libc
        INTERFACE
            LINKER:--undefined=osal_newlib_initialize
    )
endif()
