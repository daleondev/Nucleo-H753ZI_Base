#include "platform.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace
{
    struct LedState
    {
        std::string_view name;
        bool is_on;
    };

    auto led_states() -> std::array<LedState, LEDn>&
    {
        static std::array states{ LedState{ .name = "green", .is_on = false },
                                  LedState{ .name = "red", .is_on = false } };
        return states;
    }

    void print_message(std::string_view message)
    {
        std::string line{ "[sim][hal] " };
        line.append(message);
        line.push_back('\n');
        const auto written{ std::fwrite(line.data(), sizeof(char), line.size(), stdout) };
        assert(written == line.size());
        static_cast<void>(written);
        std::fflush(stdout);
    }

    bool is_valid_led(Led_TypeDef led) { return led >= LED_GREEN && led < LEDn; }
}

extern "C" {

TIM_HandleTypeDef htim2{};

HAL_StatusTypeDef HAL_Init()
{
    print_message("HAL_Init");
    return HAL_OK;
}

void SystemClock_Config() { print_message("SystemClock_Config"); }

void MPU_Config_User() { print_message("MPU_Config_User"); }

void SCB_EnableICache() { print_message("SCB_EnableICache"); }

void SCB_EnableDCache() { print_message("SCB_EnableDCache"); }

void MX_GPIO_Init() { print_message("MX_GPIO_Init"); }

void MX_ETH_Init() { print_message("MX_ETH_Init"); }

void MX_RTC_Init() { print_message("MX_RTC_Init"); }

void MX_TIM2_Init() { print_message("MX_TIM2_Init"); }

void MX_RNG_Init() { print_message("MX_RNG_Init"); }

HAL_StatusTypeDef platform_init_libc_locks() { return HAL_OK; }

int32_t BSP_LED_Init(Led_TypeDef led)
{
    if (!is_valid_led(led)) {
        return BSP_ERROR_UNKNOWN;
    }

    auto& state{ led_states()[static_cast<std::size_t>(led)] };
    state.is_on = false;
    print_message(std::string{ "BSP_LED_Init(" } + std::string{ state.name } + ')');
    return BSP_ERROR_NONE;
}

int32_t BSP_LED_Toggle(Led_TypeDef led)
{
    if (!is_valid_led(led)) {
        return BSP_ERROR_UNKNOWN;
    }

    auto& state{ led_states()[static_cast<std::size_t>(led)] };
    state.is_on = !state.is_on;
    print_message(std::string{ "BSP_LED_Toggle(" } + std::string{ state.name } + ") -> " +
                  (state.is_on ? "on" : "off"));
    return BSP_ERROR_NONE;
}

int32_t BSP_PB_Init(Button_TypeDef button, ButtonMode_TypeDef button_mode)
{
    print_message(std::string{ "BSP_PB_Init(button=" } + std::to_string(static_cast<int>(button)) +
                  ", mode=" + std::to_string(static_cast<int>(button_mode)) + ')');
    return BSP_ERROR_NONE;
}

int32_t BSP_COM_Init(COM_TypeDef com, COM_InitTypeDef* com_init)
{
    const auto baud_rate{ com_init != nullptr ? com_init->BaudRate : 0U };
    print_message(std::string{ "BSP_COM_Init(com=" } + std::to_string(static_cast<int>(com)) +
                  ", baud=" + std::to_string(baud_rate) + ')');
    return BSP_ERROR_NONE;
}

HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef* timer_handle)
{
    static_cast<void>(timer_handle);
    print_message("HAL_TIM_Base_Start");
    return HAL_OK;
}

void Error_Handler()
{
    std::fputs("[sim][hal] Error_Handler\n", stderr);
    std::fflush(stderr);
    std::abort();
}
}
