#include "hal.hpp"

#include <cstdint>

#if defined(HAL_PLATFORM_LINUX)

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace
{
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
