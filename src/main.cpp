#include "main.h"
#include "fdcan.h"
#include "gpio.h"
#include "rng.h"
#include "rtc.h"
#include "tim.h"

#include <tx_api.h>

extern "C" {
extern COM_InitTypeDef BspCOMInit;
extern void SystemClock_Config(void);
extern void MPU_Config_User(void);
}

namespace
{
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
    }
} // namespace

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