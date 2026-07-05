#include "hal.hpp"

#include <cstdint>

#if defined(HAL_PLATFORM_STM32)

#include <bit>
#include <cstddef>

namespace
{
    constexpr std::uint32_t RED_LED_PIN_NUMBER{
        std::countr_zero(static_cast<std::uint32_t>(LED3_PIN))
    };
    constexpr std::uint32_t GPIO_MODE_BITS{ 2U };
    constexpr std::uint32_t UART_SPIN_LIMIT{ 1'000'000U };

    auto panic_uart_write_character(char character) noexcept -> void
    {
        constexpr std::uint32_t enabled_mask{ USART_CR1_UE | USART_CR1_TE };
        if ((COM1_UART->CR1 & enabled_mask) != enabled_mask) {
            return;
        }

        std::uint32_t remaining{ UART_SPIN_LIMIT };
        while ((COM1_UART->ISR & USART_ISR_TXE_TXFNF) == 0U && remaining != 0U) {
            --remaining;
        }
        if (remaining != 0U) {
            COM1_UART->TDR = static_cast<std::uint8_t>(character);
        }
    }

    auto panic_write(const char* text) noexcept -> void
    {
        if (text == nullptr) {
            return;
        }
        while (*text != '\0') {
            panic_uart_write_character(*text++);
        }
    }

    auto panic_write_line(std::uint32_t line) noexcept -> void
    {
        char digits[10]{};
        std::size_t count{};
        do {
            digits[count++] = static_cast<char>('0' + line % 10U);
            line /= 10U;
        } while (line != 0U);
        while (count != 0U) {
            panic_uart_write_character(digits[--count]);
        }
    }

    auto configure_red_led() noexcept -> void
    {
        LED3_GPIO_CLK_ENABLE();
        __DSB();

        constexpr std::uint32_t mode_shift{ RED_LED_PIN_NUMBER * GPIO_MODE_BITS };
        constexpr std::uint32_t mode_mask{ 0x3U << mode_shift };
        LED3_GPIO_PORT->MODER = (LED3_GPIO_PORT->MODER & ~mode_mask) | (0x1U << mode_shift);
        LED3_GPIO_PORT->OTYPER &= ~LED3_PIN;
        LED3_GPIO_PORT->OSPEEDR &= ~mode_mask;
        LED3_GPIO_PORT->PUPDR &= ~mode_mask;
        LED3_GPIO_PORT->BSRR = static_cast<std::uint32_t>(LED3_PIN) << 16U;
    }

    auto set_red_led(bool on) noexcept -> void
    {
        LED3_GPIO_PORT->BSRR = on ? LED3_PIN : static_cast<std::uint32_t>(LED3_PIN) << 16U;
    }

    auto panic_delay() noexcept -> void
    {
        const std::uint32_t iterations{ SystemCoreClock >= 1'000'000U ? SystemCoreClock / 32U
                                                                      : 2'000'000U };
        for (std::uint32_t iteration{}; iteration < iterations; ++iteration) {
            __NOP();
        }
    }

    auto write_panic_info(const HalPanicInfo* info) noexcept -> void
    {
        panic_write("\r\n[hal][panic] ");
        panic_write(info != nullptr && info->message != nullptr ? info->message : "fatal error");
        if (info != nullptr && info->file != nullptr) {
            panic_write("\r\n  at ");
            panic_write(info->file);
            if (info->line != 0U) {
                panic_uart_write_character(':');
                panic_write_line(info->line);
            }
            if (info->function != nullptr) {
                panic_write(" (");
                panic_write(info->function);
                panic_uart_write_character(')');
            }
        }
        panic_write("\r\n");
    }
}

#elif defined(HAL_PLATFORM_LINUX)

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unistd.h>

namespace
{
    auto print_message(std::string_view message) -> void
    {
        std::string line{ "[sim][hal] " };
        line.append(message);
        line.push_back('\n');
        const auto written{ std::fwrite(line.data(), sizeof(char), line.size(), stdout) };
        assert(written == line.size());
        static_cast<void>(written);
        std::fflush(stdout);
    }

    auto panic_write(const char* text) noexcept -> void
    {
        if (text == nullptr) {
            return;
        }

        std::size_t length{};
        while (text[length] != '\0') {
            ++length;
        }
        while (length != 0U) {
            const ssize_t written{ ::write(STDERR_FILENO, text, length) };
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return;
            }
            if (written == 0) {
                return;
            }
            text += written;
            length -= static_cast<std::size_t>(written);
        }
    }

    auto panic_write_line(std::uint32_t line) noexcept -> void
    {
        char digits[10]{};
        std::size_t count{};
        do {
            digits[count++] = static_cast<char>('0' + line % 10U);
            line /= 10U;
        } while (line != 0U);
        while (count != 0U) {
            const char character[2]{ digits[--count], '\0' };
            panic_write(character);
        }
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

}

#else
#error "Unsupported HAL platform"
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

    auto panic(const char* message, std::source_location location) noexcept -> void
    {
        const HalPanicInfo info{
            .message = message,
            .file = location.file_name(),
            .function = location.function_name(),
            .line = location.line(),
        };
        hal_panic_handler(&info);
    }
}

extern "C" [[noreturn]] void Error_Handler()
{
    const HalPanicInfo info{
        .message = "HAL Error_Handler invoked",
        .file = nullptr,
        .function = nullptr,
        .line = 0U,
    };
    hal_panic_handler(&info);
}

#if defined(HAL_PLATFORM_STM32)

extern "C" [[gnu::weak, gnu::noinline, noreturn]] void hal_panic_handler(
  const HalPanicInfo* info) noexcept
{
    __disable_irq();
    __DSB();
    write_panic_info(info);
    configure_red_led();

    while (true) {
        set_red_led(true);
        panic_delay();
        set_red_led(false);
        panic_delay();
    }
}

#elif defined(HAL_PLATFORM_LINUX)

extern "C" [[gnu::weak, gnu::noinline, noreturn]] void hal_panic_handler(
  const HalPanicInfo* info) noexcept
{
    panic_write("[hal][panic] ");
    panic_write(info != nullptr && info->message != nullptr ? info->message : "fatal error");
    if (info != nullptr && info->file != nullptr) {
        panic_write("\n  at ");
        panic_write(info->file);
        if (info->line != 0U) {
            panic_write(":");
            panic_write_line(info->line);
        }
        if (info->function != nullptr) {
            panic_write(" (");
            panic_write(info->function);
            panic_write(")");
        }
    }
    panic_write("\n");
    std::abort();
}

#endif
