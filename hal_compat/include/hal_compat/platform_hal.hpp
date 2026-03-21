#pragma once

#if defined(HAL_COMPAT_STM32)
#include "hal_compat_stm32.hpp"
#elif defined(HAL_COMPAT_LINUX)
#include "hal_compat_linux.hpp"
#else
#error "Unsupported HalCompat platform"
#endif
