#pragma once

#include <tx_api.h>

#include <string>
#include <string_view>

namespace osal
{
    class Mutex
    {
      public:
        Mutex();
        Mutex(std::string_view name);
        ~Mutex();

        Mutex(const Mutex&) = delete;
        auto operator=(const Mutex&) -> Mutex& = delete;

        Mutex(Mutex&&) = delete;
        auto operator=(Mutex&&) -> Mutex& = delete;

        auto unlock() -> void;
        auto lock() -> void;
        auto tryLock() -> bool;

      private:
        std::string m_name;
        TX_MUTEX m_mutex{};
    };
}
