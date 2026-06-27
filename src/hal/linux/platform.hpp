#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// These declarations intentionally mirror the STM32 HAL C ABI.
// NOLINTBEGIN(modernize-use-using,performance-enum-size)

typedef enum
{
    HAL_OK = 0x00U,
    HAL_ERROR = 0x01U,
} HAL_StatusTypeDef;

typedef struct
{
    uint32_t Instance;
} TIM_HandleTypeDef;

extern TIM_HandleTypeDef htim2;

typedef enum
{
    LED_GREEN = 0,
    LED_RED,
    LEDn,
} Led_TypeDef;

typedef enum
{
    BUTTON_USER = 0,
    BUTTONn,
} Button_TypeDef;

typedef enum
{
    BUTTON_MODE_GPIO = 0,
    BUTTON_MODE_EXTI = 1,
} ButtonMode_TypeDef;

typedef enum
{
    COM1 = 0,
    COMn,
} COM_TypeDef;

enum
{
    COM_WORDLENGTH_8B = 8U,
    COM_STOPBITS_1 = 1U,
    COM_PARITY_NONE = 0U,
    COM_HWCONTROL_NONE = 0U,
    BSP_ERROR_NONE = 0,
    BSP_ERROR_UNKNOWN = -1,
};

typedef struct
{
    uint32_t BaudRate;
    uint32_t WordLength;
    uint32_t StopBits;
    uint32_t Parity;
    uint32_t HwFlowCtl;
} COM_InitTypeDef;

HAL_StatusTypeDef HAL_Init(void);
void SystemClock_Config(void);
void MPU_Config_User(void);
void SCB_EnableICache(void);
void SCB_EnableDCache(void);

void MX_GPIO_Init(void);
void MX_ETH_Init(void);
void MX_RTC_Init(void);
void MX_TIM2_Init(void);
void MX_RNG_Init(void);
void MX_FDCAN1_Init(void);
HAL_StatusTypeDef platform_init_libc_locks(void);

int32_t BSP_LED_Init(Led_TypeDef led);
int32_t BSP_LED_Toggle(Led_TypeDef led);
int32_t BSP_PB_Init(Button_TypeDef button, ButtonMode_TypeDef button_mode);
int32_t BSP_COM_Init(COM_TypeDef com, COM_InitTypeDef* com_init);

HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef* timer_handle);
void Error_Handler(void);

// NOLINTEND(modernize-use-using,performance-enum-size)

#ifdef __cplusplus
}
#endif
