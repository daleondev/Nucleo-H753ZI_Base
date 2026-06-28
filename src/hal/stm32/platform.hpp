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

typedef struct
{
    uint64_t ticks;
    uint64_t ticks_per_second;
    uint64_t modulus;
} PlatformHighResolutionCounter;

HAL_StatusTypeDef platform_get_system_time(int64_t* seconds_since_epoch, uint32_t* nanoseconds);
HAL_StatusTypeDef platform_get_high_resolution_counter(PlatformHighResolutionCounter* counter);

#ifdef __cplusplus
}
#endif
