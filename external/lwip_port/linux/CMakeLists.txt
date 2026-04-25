# Linux netif port: vendors lwIP's upstream tapif against /dev/net/tun.

set(_lwip_unix_port ${CMAKE_SOURCE_DIR}/external/lwip/contrib/ports/unix/port)

add_library(LwipPortLinux STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/tapif_linux.c
    ${CMAKE_CURRENT_LIST_DIR}/src/lwip_port_linux.c
)

target_include_directories(LwipPortLinux PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${_lwip_unix_port}/include
)

target_compile_definitions(LwipPortLinux PUBLIC
    LWIP_UNIX_LINUX=1
)

target_link_libraries(LwipPortLinux PUBLIC lwip)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(LwipPortLinux PRIVATE
        -Wno-unused-parameter
        -Wno-format-truncation
        -Wno-implicit-function-declaration
    )
endif()
