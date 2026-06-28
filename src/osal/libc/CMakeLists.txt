add_library(osal_libc STATIC)
add_library(osal::libc ALIAS osal_libc)

target_sources(osal_libc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libc.c
)

target_link_libraries(osal_libc
    PUBLIC
        threadx
)

target_compile_features(osal_libc
    PUBLIC
        c_std_11
)
