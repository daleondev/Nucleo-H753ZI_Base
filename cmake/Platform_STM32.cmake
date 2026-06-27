set(THREADX_ARCH cortex_m7)
set(THREADX_TOOLCHAIN gnu)
set(TX_USER_FILE ${PROJECT_SOURCE_DIR}/external/CubeMX/Inc/tx_user.h)
add_subdirectory(${PROJECT_SOURCE_DIR}/external/threadx)

# This setting changes ThreadX-visible structures and must be consistent for
# the kernel and every consumer of tx_api.h.
target_compile_definitions(threadx PUBLIC TX_ENABLE_STACK_CHECKING)

add_library(PlatformConfig INTERFACE)

target_include_directories(PlatformConfig
    INTERFACE
        ${PROJECT_SOURCE_DIR}/external/CubeMX/Inc
        ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Inc
        ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Inc/Legacy
        ${PROJECT_SOURCE_DIR}/external/stm32h7xx-nucleo-bsp
        ${PROJECT_SOURCE_DIR}/external/cmsis-core/CMSIS/Core/Include
        ${PROJECT_SOURCE_DIR}/external/cmsis-device-h7/Include
)

target_compile_definitions(PlatformConfig
    INTERFACE
        USE_PWR_LDO_SUPPLY
        USE_HAL_DRIVER
        STM32H753xx
        $<$<CONFIG:Debug>:DEBUG>
)

target_link_libraries(PlatformConfig
    INTERFACE
        threadx
)

# Startup and ThreadX low-level objects must be present directly in the final
# link. A static archive is insufficient because their references appear only
# after GNU ld reaches the ThreadX archive.
add_library(Platform OBJECT
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/main.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/eth.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/gpio.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/rng.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/rtc.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/tim.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/fdcan.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/stm32h7xx_it.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/stm32h7xx_hal_msp.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/stm32h7xx_hal_timebase_tim.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/sysmem.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/syscalls.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/system_stm32h7xx.c
    ${PROJECT_SOURCE_DIR}/external/CubeMX/Src/tx_initialize_low_level.S
    ${PROJECT_SOURCE_DIR}/external/CubeMX/startup_stm32h753xx.s
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_tim.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_tim_ex.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_cortex.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_eth.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_eth_ex.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_fdcan.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rcc.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rcc_ex.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_flash.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_flash_ex.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_gpio.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_hsem.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_dma.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_dma_ex.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_mdma.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_pwr.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_pwr_ex.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_i2c.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_i2c_ex.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_exti.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rng.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rng_ex.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rtc.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rtc_ex.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_usart.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_usart_ex.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_uart.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_uart_ex.c
    ${PROJECT_SOURCE_DIR}/external/stm32h7xx-nucleo-bsp/stm32h7xx_nucleo.c
)

target_compile_features(Platform
    PUBLIC
        c_std_11
)

target_link_libraries(Platform
    PUBLIC
        PlatformConfig
)
