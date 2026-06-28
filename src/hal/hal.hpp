#pragma once

#if defined(HAL_PLATFORM_LINUX)
#include "linux/platform.hpp"
#elif defined(HAL_PLATFORM_STM32)
#include "stm32/platform.hpp"
#endif