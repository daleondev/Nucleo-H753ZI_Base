#include "hal_compat/hal_compat_linux.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace
{
    struct LedState
    {
        const char* name;
        bool is_on;
    };

    std::array<LedState, LEDn> led_states{ { { "green", false }, { "red", false } } };

    void PrintMessage(const char* message)
    {
        std::printf("[sim][hal] %s\n", message);
        std::fflush(stdout);
    }

    bool IsValidLed(Led_TypeDef led) { return led >= LED_GREEN && led < LEDn; }
}

extern "C" {

TIM_HandleTypeDef htim2{};
COM_InitTypeDef BspCOMInit{};

HAL_StatusTypeDef HAL_Init(void)
{
    PrintMessage("HAL_Init");
    return HAL_OK;
}

void SystemClock_Config(void) { PrintMessage("SystemClock_Config"); }

void MPU_Config_User(void) { PrintMessage("MPU_Config_User"); }

void SCB_EnableICache(void) { PrintMessage("SCB_EnableICache"); }

void SCB_EnableDCache(void) { PrintMessage("SCB_EnableDCache"); }

void MX_GPIO_Init(void) { PrintMessage("MX_GPIO_Init"); }

void MX_ETH_Init(void) { PrintMessage("MX_ETH_Init"); }

void MX_RTC_Init(void) { PrintMessage("MX_RTC_Init"); }

void MX_TIM2_Init(void) { PrintMessage("MX_TIM2_Init"); }

void MX_RNG_Init(void) { PrintMessage("MX_RNG_Init"); }

void MX_FDCAN1_Init(void) { PrintMessage("MX_FDCAN1_Init"); }

int32_t BSP_LED_Init(Led_TypeDef led)
{
    if (!IsValidLed(led)) {
        return BSP_ERROR_UNKNOWN;
    }

    led_states[static_cast<size_t>(led)].is_on = false;
    std::printf("[sim][hal] BSP_LED_Init(%s)\n", led_states[static_cast<size_t>(led)].name);
    std::fflush(stdout);
    return BSP_ERROR_NONE;
}

int32_t BSP_LED_Toggle(Led_TypeDef led)
{
    if (!IsValidLed(led)) {
        return BSP_ERROR_UNKNOWN;
    }

    auto& state = led_states[static_cast<size_t>(led)];
    state.is_on = !state.is_on;

    std::printf("[sim][hal] BSP_LED_Toggle(%s) -> %s\n", state.name, state.is_on ? "on" : "off");
    std::fflush(stdout);
    return BSP_ERROR_NONE;
}

int32_t BSP_PB_Init(Button_TypeDef button, ButtonMode_TypeDef button_mode)
{
    std::printf("[sim][hal] BSP_PB_Init(button=%d, mode=%d)\n", button, button_mode);
    std::fflush(stdout);
    return BSP_ERROR_NONE;
}

int32_t BSP_COM_Init(COM_TypeDef com, COM_InitTypeDef* com_init)
{
    if (com_init != nullptr) {
        BspCOMInit = *com_init;
    }

    std::printf(
      "[sim][hal] BSP_COM_Init(com=%d, baud=%lu)\n", com, static_cast<unsigned long>(BspCOMInit.BaudRate));
    std::fflush(stdout);
    return BSP_ERROR_NONE;
}

HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef* timer_handle)
{
    static_cast<void>(timer_handle);
    PrintMessage("HAL_TIM_Base_Start");
    return HAL_OK;
}

void Error_Handler(void)
{
    std::fprintf(stderr, "[sim][hal] Error_Handler\n");
    std::fflush(stderr);
    std::abort();
}
}
