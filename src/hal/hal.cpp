#include "hal.hpp"

#include <cstdint>

#if defined(HAL_PLATFORM_LINUX)

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

void Error_Handler()
{
    std::fputs("[sim][hal] Error_Handler\n", stderr);
    std::fflush(stderr);
    std::abort();
}
}

#endif

namespace hal
{
    auto initialize() noexcept -> void
    {
        constexpr std::uint32_t com_baud_rate{ 115200U };

        MPU_Config_User();
        SCB_EnableICache();
        SCB_EnableDCache();

        if (HAL_Init() != HAL_OK) {
            Error_Handler();
        }

        SystemClock_Config();

        MX_GPIO_Init();
        MX_ETH_Init();
        MX_RTC_Init();
        MX_TIM2_Init();
        MX_RNG_Init();

        if (BSP_LED_Init(LED_GREEN) != BSP_ERROR_NONE ||
            BSP_LED_Init(LED_RED) != BSP_ERROR_NONE ||
            BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI) != BSP_ERROR_NONE) {
            Error_Handler();
        }

        COM_InitTypeDef bsp_com_init{};
        bsp_com_init.BaudRate = com_baud_rate;
        bsp_com_init.WordLength = COM_WORDLENGTH_8B;
        bsp_com_init.StopBits = COM_STOPBITS_1;
        bsp_com_init.Parity = COM_PARITY_NONE;
        bsp_com_init.HwFlowCtl = COM_HWCONTROL_NONE;

        if (BSP_COM_Init(COM1, &bsp_com_init) != BSP_ERROR_NONE) {
            Error_Handler();
        }
    }
}
