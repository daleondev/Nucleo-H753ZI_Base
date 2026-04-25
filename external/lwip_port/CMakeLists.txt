# Wrapper around the lwIP submodule that produces a single `Lwip` interface
# library. The upstream `external/lwip` submodule is left untouched: we pull in
# its source-list helper (`Filelists.cmake`) directly instead of doing
# `add_subdirectory()`.

set(LWIP_DIR ${CMAKE_SOURCE_DIR}/external/lwip)
set(LWIP_PORT_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR}/common)

# Defer to upstream Filelists.cmake to define lwipnoapps_SRCS etc. without
# building the example-app target.
include(${LWIP_DIR}/src/Filelists.cmake)

# We do not use IPv6/PPP/SNMP/HTTPD - cherry-pick from the source list.
set(_lwip_sources
    ${lwipcore_SRCS}
    ${lwipcore4_SRCS}
    ${lwipapi_SRCS}
    ${LWIP_DIR}/src/netif/ethernet.c
)

add_library(lwip STATIC ${_lwip_sources})

target_include_directories(lwip
    PUBLIC
        ${LWIP_DIR}/src/include
        ${LWIP_PORT_COMMON_DIR}/include
)

target_compile_definitions(lwip
    PUBLIC
        LWIP_NO_UNISTD_H=0
)

# Silence aggressive third-party warnings without affecting the application.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lwip PRIVATE
        -Wno-address
        -Wno-unused-parameter
        -Wno-unused-but-set-variable
        -Wno-sign-compare
    )
endif()

# OS abstraction (sys_arch) for both targets is implemented on top of ThreadX.
add_subdirectory(threadx)
target_link_libraries(lwip PUBLIC LwipPortThreadx)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(linux)
    add_library(LwipPort INTERFACE)
    target_link_libraries(LwipPort INTERFACE lwip LwipPortLinux)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    add_subdirectory(stm32)
    add_library(LwipPort INTERFACE)
    target_link_libraries(LwipPort INTERFACE lwip LwipPortStm32)
else()
    message(FATAL_ERROR "lwip_port: unsupported system ${CMAKE_SYSTEM_NAME}")
endif()
