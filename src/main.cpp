#include "main.h"
#include "fdcan.h"
#include "gpio.h"
#include "rng.h"
#include "rtc.h"
#include "tim.h"

#include <array>
#include <tx_api.h>

extern "C" {
extern COM_InitTypeDef BspCOMInit;
extern void SystemClock_Config(void);
extern void MPU_Config_User(void);
}

namespace
{
    constexpr size_t MAIN_THREAD_STACK_SIZE{ 4096 };
    constexpr uint32_t MAIN_THREAD_PRIO{ 15 };

    std::array<std::byte, MAIN_THREAD_STACK_SIZE> main_thread_stack;
    std::array<char, 16> main_thread_name{ "Main Thread" };

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

    void InitializePeripherals()
    {
        MX_GPIO_Init();
        MX_RTC_Init();
        MX_TIM2_Init();
        MX_RNG_Init();
        MX_FDCAN1_Init();
    }

    void InitializeBoardSupport()
    {
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
    }

    void StartRuntimeServices()
    {
        if (HAL_TIM_Base_Start(&htim2) != HAL_OK) {
            Error_Handler();
        }

        AssertTxCall(tx_thread_stack_error_notify([](TX_THREAD* thread) {
            printf("Thread %s stack overflow detected\r\n", thread->tx_thread_name);
            Error_Handler();
        }));
    }

    void TxMain(ULONG)
    {
        const auto blink_period_ticks{ MillisecondsToTicks(100) };

        while (true) {
            BSP_LED_Toggle(LED_GREEN);
            BSP_LED_Toggle(LED_RED);
            tx_thread_sleep(blink_period_ticks);
        }
    }
} // namespace

extern "C" void tx_application_define(void* first_unused_memory)
{
    static_cast<void>(first_unused_memory);

    AssertTxCall(tx_thread_create(&main_thread,
                                  reinterpret_cast<CHAR*>(main_thread_name.data()),
                                  TxMain,
                                  0,
                                  main_thread_stack.data(),
                                  main_thread_stack.size(),
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

    InitializePeripherals();
    InitializeBoardSupport();
    StartRuntimeServices();

    tx_kernel_enter();

    Error_Handler();
}