# Wrapper that builds the open62541 submodule with no architecture support and
# layers the lwIP eventloop on top, alongside our ThreadX-based clock plugin.
#
# This produces a single `Open62541Lwip` library that downstream code links
# against. The upstream submodule is configured with UA_ARCHITECTURE=none so
# we avoid pulling in POSIX/Win32/FreeRTOS/Zephyr glue, then we manually add
# the lwIP arch sources (which are arch-agnostic apart from the LWIP define).

set(_open62541_dir   ${CMAKE_SOURCE_DIR}/external/open62541)
set(_open62541_arch  ${_open62541_dir}/arch)

# Configure the open62541 build before adding the subdirectory.
set(UA_ARCHITECTURE     "none"     CACHE STRING "" FORCE)
set(UA_ENABLE_AMALGAMATION  OFF    CACHE BOOL   "" FORCE)
set(BUILD_SHARED_LIBS       OFF    CACHE BOOL   "" FORCE)
set(UA_ENABLE_PUBSUB        OFF    CACHE BOOL   "" FORCE)
set(UA_ENABLE_DISCOVERY     OFF    CACHE BOOL   "" FORCE)
set(UA_ENABLE_DISCOVERY_MULTICAST "OFF" CACHE STRING "" FORCE)
set(UA_ENABLE_ENCRYPTION    "OFF"  CACHE STRING "" FORCE)
set(UA_ENABLE_JSON_ENCODING OFF    CACHE BOOL   "" FORCE)
set(UA_ENABLE_XML_ENCODING  OFF    CACHE BOOL   "" FORCE)
set(UA_ENABLE_HISTORIZING   OFF    CACHE BOOL   "" FORCE)
set(UA_ENABLE_SUBSCRIPTIONS_EVENTS OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_DIAGNOSTICS   OFF    CACHE BOOL   "" FORCE)
set(UA_ENABLE_PARSING       OFF    CACHE BOOL   "" FORCE)
set(UA_ENABLE_NODESET_INJECTOR OFF CACHE BOOL   "" FORCE)
set(UA_NAMESPACE_ZERO      "REDUCED" CACHE STRING "" FORCE)
set(UA_MULTITHREADING      0       CACHE STRING "" FORCE)
set(UA_BUILD_EXAMPLES       OFF    CACHE BOOL   "" FORCE)
set(UA_BUILD_TOOLS          OFF    CACHE BOOL   "" FORCE)
set(UA_BUILD_UNIT_TESTS     OFF    CACHE BOOL   "" FORCE)
set(UA_FORCE_WERROR         OFF    CACHE BOOL   "" FORCE)
set(UA_ENABLE_DEBUG_SANITIZER OFF  CACHE BOOL   "" FORCE)

# Tell config.h.in that we are running on lwIP. This cmakedefine only fires
# when the named CMake variable is truthy at configure time, so setting it
# *before* add_subdirectory is enough.
set(UA_ARCHITECTURE_LWIP 1)

# Satisfy upstream's `find_package(LWIP REQUIRED)` call: we already provide
# the lwIP build through our `lwip` target, so we just point at its sources
# and set the library variable to our target name (CMake resolves it later).
set(LWIP_FOUND        TRUE             CACHE INTERNAL "" FORCE)
set(_lwip_include_dirs
    ${CMAKE_SOURCE_DIR}/external/lwip/src/include
    ${CMAKE_SOURCE_DIR}/external/lwip_port/common/include
    ${CMAKE_SOURCE_DIR}/external/lwip_port/threadx/include
    ${CMAKE_SOURCE_DIR}/external/threadx/common/inc
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND _lwip_include_dirs ${CMAKE_SOURCE_DIR}/external/threadx/ports/linux/gnu/inc)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    list(APPEND _lwip_include_dirs ${CMAKE_SOURCE_DIR}/external/threadx/ports/cortex_m7/gnu/inc)
endif()
set(LWIP_INCLUDE_DIRS ${_lwip_include_dirs} CACHE INTERNAL "" FORCE)
set(LWIP_LIBRARIES    lwip             CACHE INTERNAL "" FORCE)

# Neutralise upstream's install/export rules: we only consume open62541 as
# a transitive static archive of `Open62541Lwip`, never install it standalone.
# Overriding install() as a no-op before add_subdirectory swallows the
# install(TARGETS ... EXPORT ...) calls that pull our internal `lwip` target
# (and its transitive deps) into a published export set.
function(install)
endfunction()
function(export)
endfunction()

add_subdirectory(${_open62541_dir} ${CMAKE_BINARY_DIR}/external/open62541)

# The open62541 target's compile flags include `-Wno-error` etc; just make
# sure downstream consumers can find the public headers from the build tree.
target_include_directories(open62541-object PUBLIC
    $<BUILD_INTERFACE:${_open62541_dir}/include>
    $<BUILD_INTERFACE:${_open62541_dir}/plugins/include>
    $<BUILD_INTERFACE:${_open62541_dir}/deps>
    $<BUILD_INTERFACE:${_open62541_dir}/src>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/external/open62541/src_generated>
)

# ---- Open62541Lwip -------------------------------------------------------

# Upstream already compiles arch/lwip/eventloop_lwip{,_tcp,_udp}.c and
# arch/common/{timer,eventloop_common}.c into the open62541 target whenever
# UA_ARCHITECTURE_LWIP is set, so we only add our own clock plugin here.
# Inject clock_threadx.c directly into the upstream open62541-plugins
# OBJECT library so the static archive resolves UA_DateTime_now*/local
# offset symbols without requiring linker --start-group tricks.
target_sources(open62541-plugins PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/common/clock_threadx.c
)

add_library(Open62541Lwip INTERFACE)

target_include_directories(Open62541Lwip INTERFACE
    ${_open62541_dir}/include
    ${_open62541_dir}/plugins/include
    ${_open62541_dir}/deps
    ${_open62541_dir}/src
    ${_open62541_arch}
    ${CMAKE_BINARY_DIR}/external/open62541/src_generated
)

target_compile_definitions(Open62541Lwip INTERFACE
    # UA_ARCHITECTURE_LWIP is already injected by the upstream config.h
    # (driven by the cache variable we set above).
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The upstream lwIP eventloop branches on `defined(UA_ARCHITECTURE_LWIP)
    # && !defined(UA_ARCHITECTURE_FREERTOS)` to decide whether it can call
    # POSIX clock_gettime() and use struct timespec from <time.h>. Newlib on
    # arm-none-eabi has no clock_gettime, so we set the FreeRTOS marker
    # everywhere bare-metal to take the non-POSIX branch and let our
    # `clock_threadx.c` plugin own time.
    target_compile_definitions(open62541-plugins PRIVATE UA_ARCHITECTURE_FREERTOS)
    target_compile_definitions(Open62541Lwip     INTERFACE UA_ARCHITECTURE_FREERTOS)
endif()

target_link_libraries(Open62541Lwip
    INTERFACE
        open62541
        lwip
        threadx
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(open62541-plugins PRIVATE
        -Wno-unused-parameter
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-implicit-function-declaration
        -Wno-sign-compare
    )
endif()
