cmake_minimum_required(VERSION 3.22)

project(Application LANGUAGES CXX C ASM)
set(CMAKE_PROJECT_NAME Application)

# --- Executable ---
add_executable(${PROJECT_NAME} src/main.cpp)

# --- Sources ---
target_sources(${PROJECT_NAME} 
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/main.c
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/gpio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/rng.c
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/rtc.c
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/tim.c
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/fdcan.c
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/stm32h7xx_it.c
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/stm32h7xx_hal_msp.c
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/stm32h7xx_hal_timebase_tim.c
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/sysmem.c
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/syscalls.c
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/startup_stm32h753xx.s
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/system_stm32h7xx.c
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Src/tx_initialize_low_level.S
)

# --- Includes ---
target_include_directories(${PROJECT_NAME} 
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Inc
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/NetXDuo/App
        ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/NetXDuo/Target
)

set(TX_USER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/external/CubeMX/Inc/tx_user.h)
include("cmake/Platform_STM32.cmake")

target_link_libraries(${PROJECT_NAME} 
    PRIVATE
        Platform
)

target_compile_features(${PROJECT_NAME}
    PUBLIC
        cxx_std_23
)

# --- Post-build steps ---
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "Generating hex and bin files, and printing size"
)