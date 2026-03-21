set(THREADX_ARCH cortex_m7)
set(THREADX_TOOLCHAIN gnu)
set(TX_ENABLE_STACK_CHECKING ON)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/threadx)

add_library(Platform INTERFACE)

target_sources(Platform
    INTERFACE
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_tim.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_tim_ex.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_cortex.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_eth.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_eth_ex.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rcc.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rcc_ex.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_flash.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_flash_ex.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_gpio.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_hsem.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_dma.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_dma_ex.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_mdma.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_pwr.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_pwr_ex.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_i2c.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_i2c_ex.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_exti.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rng.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rng_ex.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rtc.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_rtc_ex.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_usart.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_usart_ex.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_uart_ex.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Src/stm32h7xx_hal_uart.c
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-nucleo-bsp/stm32h7xx_nucleo.c
)

target_include_directories(Platform 
    INTERFACE
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Inc
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-hal-driver/Inc/Legacy
        ${CMAKE_SOURCE_DIR}/external/stm32h7xx-nucleo-bsp
        ${CMAKE_SOURCE_DIR}/external/cmsis-core/CMSIS/Core/Include
        ${CMAKE_SOURCE_DIR}/external/cmsis-device-h7/Include
)

target_compile_definitions(Platform 
    INTERFACE
        USE_PWR_LDO_SUPPLY
        USE_NUCLEO_64
        USE_HAL_DRIVER
        STM32H753xx
        $<$<CONFIG:Debug>:DEBUG>

        TX_INCLUDE_USER_DEFINE_FILE
)

target_link_libraries(Platform
    INTERFACE
        threadx
)
