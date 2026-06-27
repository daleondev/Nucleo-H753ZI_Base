#include "Semaphore.hpp"

#include <atomic>
#include <format>

#include <cassert> // tmp

namespace osal
{
    namespace
    {
        auto next_semaphore_id() -> std::size_t
        {
            static std::atomic_size_t id{};
            return id.fetch_add(1);
        }
    }

    Semaphore::Semaphore()
      : Semaphore(std::format("Semaphore_{}", next_semaphore_id()))
    {
    }

    Semaphore::Semaphore(std::string_view name)
      : m_name{ name }
    {
        auto ret{ tx_semaphore_create(&m_semaphore, const_cast<CHAR*>(m_name.data()), 0) };
        assert(ret == TX_SUCCESS);
    }

    Semaphore::~Semaphore()
    {
        auto ret{ tx_semaphore_delete(&m_semaphore) };
        assert(ret == TX_SUCCESS);
    }

    auto Semaphore::put() -> void
    {
        auto ret{ tx_semaphore_put(&m_semaphore) };
        assert(ret == TX_SUCCESS);
    }

    auto Semaphore::get() -> void
    {
        auto ret{ tx_semaphore_get(&m_semaphore, TX_WAIT_FOREVER) };
        assert(ret == TX_SUCCESS);
    }

    auto Semaphore::tryGet() -> bool
    {
        auto ret{ tx_semaphore_get(&m_semaphore, TX_NO_WAIT) };
        assert(ret != TX_WAIT_ERROR && ret != TX_SEMAPHORE_ERROR);
        return ret == TX_SUCCESS;
    }
}
