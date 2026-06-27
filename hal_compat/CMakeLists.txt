if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    set(HAL_COMPAT_SOURCE src/hal_compat_stm32.cpp)
    set(HAL_COMPAT_DEFINITION HAL_COMPAT_STM32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(HAL_COMPAT_SOURCE src/hal_compat_linux.cpp)
    set(HAL_COMPAT_DEFINITION HAL_COMPAT_LINUX)
else()
    message(FATAL_ERROR "Unsupported HAL compatibility platform: ${CMAKE_SYSTEM_NAME}")
endif()

add_library(HalCompat STATIC
    ${HAL_COMPAT_SOURCE}
)

target_include_directories(HalCompat
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(HalCompat
    PUBLIC
        ${HAL_COMPAT_DEFINITION}
)

target_compile_options(HalCompat
    PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

target_link_libraries(HalCompat
    PUBLIC
        PlatformConfig
)
