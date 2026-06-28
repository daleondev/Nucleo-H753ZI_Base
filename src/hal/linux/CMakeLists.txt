target_sources(hal
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/platform.cpp
)

target_compile_definitions(hal
    PUBLIC
        HAL_PLATFORM_LINUX
)