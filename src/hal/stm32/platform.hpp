#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "eth.h"
#include "gpio.h"
#include "main.h"
#include "rng.h"
#include "rtc.h"
#include "tim.h"

void SystemClock_Config(void);
void MPU_Config_User(void);
HAL_StatusTypeDef platform_get_system_time(int64_t* seconds_since_epoch, uint32_t* nanoseconds);

#ifdef __cplusplus
}
#endif
