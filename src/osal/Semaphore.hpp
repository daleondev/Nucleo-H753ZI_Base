#pragma once

#include <tx_api.h>

#include <string>
#include <string_view>

namespace osal
{
    class Semaphore
    {
      public:
        Semaphore();
        Semaphore(std::string_view name);
        ~Semaphore();

        Semaphore(const Semaphore&) = delete;
        auto operator=(const Semaphore&) -> Semaphore& = delete;

        Semaphore(Semaphore&&) = delete;
        auto operator=(Semaphore&&) -> Semaphore& = delete;

        auto put() -> void;
        auto get() -> void;
        auto tryGet() -> bool;
        // todo: tryGetFor, tryGetUntil

      private:
        std::string m_name;
        TX_SEMAPHORE m_semaphore{};
    };
}
