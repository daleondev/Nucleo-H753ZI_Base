#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "eth.h"
#include "fdcan.h"
#include "gpio.h"
#include "main.h"
#include "rng.h"
#include "rtc.h"
#include "tim.h"

extern COM_InitTypeDef BspCOMInit;
void SystemClock_Config(void);
void MPU_Config_User(void);

#ifdef __cplusplus
}
#endif
