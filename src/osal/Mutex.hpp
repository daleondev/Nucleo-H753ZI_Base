#pragma once

#include <concepts>

#ifdef __linux__
#include "linux/Mutex.hpp"
#else
#include "stm32/Mutex.hpp"
#endif

namespace osal
{
    template<typename T>
    concept IsMutex = !std::copyable<T> && !std::movable<T> && requires(T t) {
        { t.lock() };
        { t.unlock() };
    };

    static_assert(IsMutex<Mutex>);
}