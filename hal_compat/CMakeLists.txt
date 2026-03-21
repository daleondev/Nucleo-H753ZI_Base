add_library(HalCompat STATIC)

target_sources(HalCompat
    PRIVATE
        $<$<STREQUAL:${CMAKE_SYSTEM_NAME},Generic>:${CMAKE_CURRENT_SOURCE_DIR}/src/hal_compat_stm32.cpp>
        $<$<STREQUAL:${CMAKE_SYSTEM_NAME},Linux>:${CMAKE_CURRENT_SOURCE_DIR}/src/hal_compat_linux.cpp>
)

target_include_directories(HalCompat
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        $<$<STREQUAL:${CMAKE_SYSTEM_NAME},Generic>:${CMAKE_SOURCE_DIR}/external/CubeMX/Inc>
)

target_compile_definitions(HalCompat
    PUBLIC
        $<$<STREQUAL:${CMAKE_SYSTEM_NAME},Generic>:HAL_COMPAT_STM32>
        $<$<STREQUAL:${CMAKE_SYSTEM_NAME},Linux>:HAL_COMPAT_LINUX>
)