add_library(runtime_filex STATIC)
add_library(runtime::filex ALIAS runtime_filex)

target_sources(runtime_filex
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/filesystem.cpp
)

target_include_directories(runtime_filex
    INTERFACE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(runtime_filex
    PUBLIC
        filex
        threadx
        project_compiler_settings
    PRIVATE
        hal
)

target_include_directories(runtime_filex
    BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/overlay
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_options(runtime_filex
    PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    target_include_directories(runtime_filex
        BEFORE INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/overlay
    )
    target_link_options(runtime_filex
        INTERFACE
            LINKER:--undefined=runtime_filex_initialize
    )
endif()
