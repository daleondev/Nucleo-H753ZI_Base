add_library(LwipPortThreadx STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/sys_arch.c
)

target_include_directories(LwipPortThreadx PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/lwip/src/include
    ${CMAKE_SOURCE_DIR}/external/lwip_port/common/include
)

target_link_libraries(LwipPortThreadx PUBLIC threadx)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(LwipPortThreadx PRIVATE -Wno-unused-parameter)
endif()
