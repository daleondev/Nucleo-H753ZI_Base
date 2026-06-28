#include "timer.hpp"

#include "impl/stm32/Timer.hpp"

#include "tim.h"

namespace hal::timer
{
    namespace
    {
        static const std::map<size_t, TIM_HandleTypeDef*, std::shared_ptr<Timer>> g_instances{
            { 2UZ, &htim2, nullptr }
        };
    }

    // std::shared_ptr<ITimer> create(size_t index) { auto }
}