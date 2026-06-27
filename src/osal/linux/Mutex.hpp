#pragma once

#include <mutex>

namespace osal
{
    class Mutex
    {
      public:
        Mutex() = default;
        ~Mutex() = default;
        Mutex(const Mutex&) = delete;
        Mutex(Mutex&&) = delete;
        auto operator=(const Mutex&) -> Mutex& = delete;
        auto operator=(Mutex&&) -> Mutex& = delete;

        auto lock() -> void;
        auto unlock() -> void;

      private:
        std::mutex m_mutex;
    };
}