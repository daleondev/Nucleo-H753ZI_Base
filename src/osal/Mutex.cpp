#include "Mutex.hpp"

#include <atomic>
#include <cstdio>
#include <format>

#include <cassert> // tmp

namespace osal
{
    namespace
    {
        std::atomic_size_t g_id{ 0UZ };
    }

    Mutex::Mutex()
      : Mutex(std::format("Mutex_{}", g_id++))
    {
    }

    Mutex::Mutex(std::string_view name)
      : m_name{ name }
    {
        auto ret{ tx_mutex_create(&m_mutex, const_cast<CHAR*>(m_name.data()), TX_INHERIT) };
        assert(ret == TX_SUCCESS);
    }

    Mutex::~Mutex()
    {
        auto ret{ tx_mutex_delete(&m_mutex) };
        assert(ret == TX_SUCCESS);
    }

    auto Mutex::unlock() -> void
    {
        auto ret{ tx_mutex_put(&m_mutex) };
        assert(ret == TX_SUCCESS);
    }

    auto Mutex::lock() -> void
    {
        auto ret{ tx_mutex_get(&m_mutex, TX_WAIT_FOREVER) };
        assert(ret == TX_SUCCESS);
    }

    auto Mutex::tryLock() -> bool
    {
        auto ret{ tx_mutex_get(&m_mutex, TX_NO_WAIT) };
        assert(ret != TX_WAIT_ERROR && ret != TX_MUTEX_ERROR);
        return ret == TX_SUCCESS;
    }
}