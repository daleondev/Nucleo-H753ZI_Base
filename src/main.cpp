#include "hal_compat/platform_hal.hpp"

#include <tx_api.h>

#include <array>
#include <cstdio>

namespace
{
    constexpr size_t MAIN_THREAD_STACK_SIZE{ 4096 };
    constexpr UINT MAIN_THREAD_PRIO{ 15 };
    constexpr ULONG BLINK_PERIOD_MS{ 100 };

    alignas(8) std::array<std::byte, MAIN_THREAD_STACK_SIZE> main_thread_stack{};
    CHAR main_thread_name[] = "Main Thread";
    TX_THREAD main_thread;

    constexpr ULONG MillisecondsToTicks(ULONG milliseconds)
    {
        const auto ticks = (milliseconds * TX_TIMER_TICKS_PER_SECOND + 999UL) / 1000UL;
        return ticks == 0 ? 1UL : ticks;
    }

    void AssertTxCall(UINT status)
    {
        if (status != TX_SUCCESS) {
            Error_Handler();
        }
    }

    void ThreadStackErrorHandler(TX_THREAD* thread)
    {
        const auto* thread_name = thread != TX_NULL ? thread->tx_thread_name : "Unknown";
        std::printf("Thread %s stack overflow detected\n", thread_name);
        std::fflush(stdout);
        Error_Handler();
    }

    void TxMain(ULONG)
    {
        const auto blink_period_ticks{ MillisecondsToTicks(BLINK_PERIOD_MS) };

        while (true) {
            AssertTxCall(
              static_cast<UINT>(BSP_LED_Toggle(LED_GREEN) == BSP_ERROR_NONE ? TX_SUCCESS : TX_NOT_DONE));
            AssertTxCall(
              static_cast<UINT>(BSP_LED_Toggle(LED_RED) == BSP_ERROR_NONE ? TX_SUCCESS : TX_NOT_DONE));
            tx_thread_sleep(blink_period_ticks);
        }
    }

} // namespace

extern "C" void tx_application_define(void* first_unused_memory)
{
    static_cast<void>(first_unused_memory);

    AssertTxCall(tx_thread_stack_error_notify(ThreadStackErrorHandler));
    AssertTxCall(tx_thread_create(&main_thread,
                                  main_thread_name,
                                  TxMain,
                                  0,
                                  main_thread_stack.data(),
                                  static_cast<ULONG>(main_thread_stack.size()),
                                  MAIN_THREAD_PRIO,
                                  MAIN_THREAD_PRIO,
                                  TX_NO_TIME_SLICE,
                                  TX_AUTO_START));
}

int main(void)
{
    MPU_Config_User();
    SCB_EnableICache();
    SCB_EnableDCache();

    HAL_Init();

    SystemClock_Config();

    MX_GPIO_Init();
    MX_RTC_Init();
    MX_TIM2_Init();
    MX_RNG_Init();
    MX_FDCAN1_Init();

    BSP_LED_Init(LED_GREEN);
    BSP_LED_Init(LED_RED);
    BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

    BspCOMInit.BaudRate = 115200;
    BspCOMInit.WordLength = COM_WORDLENGTH_8B;
    BspCOMInit.StopBits = COM_STOPBITS_1;
    BspCOMInit.Parity = COM_PARITY_NONE;
    BspCOMInit.HwFlowCtl = COM_HWCONTROL_NONE;

    if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE) {
        Error_Handler();
    }

    if (HAL_TIM_Base_Start(&htim2) != HAL_OK) {
        Error_Handler();
    }

    tx_kernel_enter();

    Error_Handler();
}