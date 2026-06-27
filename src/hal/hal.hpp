#pragma once

#ifdef __linux__
#include "linux/platform.hpp"
#else
#include "stm32/platform.hpp"
#endif