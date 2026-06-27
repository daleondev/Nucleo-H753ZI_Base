if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    add_library(HalCompat STATIC
        src/hal_compat_stm32.cpp
    )
    target_compile_definitions(HalCompat PUBLIC HAL_COMPAT_STM32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(HalCompat STATIC
        src/hal_compat_linux.cpp
    )
    target_compile_definitions(HalCompat PUBLIC HAL_COMPAT_LINUX)
else()
    message(FATAL_ERROR "Unsupported HAL compatibility platform: ${CMAKE_SYSTEM_NAME}")
endif()

target_include_directories(HalCompat
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(HalCompat
    PRIVATE
        cxx_std_23
)

set_target_properties(HalCompat PROPERTIES
    CXX_EXTENSIONS OFF
)

target_compile_options(HalCompat
    PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

target_link_libraries(HalCompat
    PUBLIC
        PlatformConfig
)
