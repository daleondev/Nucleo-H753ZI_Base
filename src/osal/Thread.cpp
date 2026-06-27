#include "Thread.hpp"

#include <ranges>

namespace osal
{

    Thread::~Thread()
    {
        if (m_thread.tx_thread_state != TX_COMPLETED && m_thread.tx_thread_state != TX_TERMINATED) {
            auto ret{ tx_thread_terminate(&m_thread) };
            assert(ret == TX_SUCCESS);
        }

        auto ret{ tx_thread_delete(&m_thread) };
        assert(ret == TX_SUCCESS);
    }

    auto Thread::join() -> void
    {
        if (m_joined) {
            return;
        }

        m_taskDone.get();
        const auto status{ tx_thread_terminate(&m_thread) };
        assert(status == TX_SUCCESS);
        m_joined = true;
    }

    auto Thread::run(ULONG id) -> VOID
    {
        Thread* self{ nullptr };
        {
            std::scoped_lock lock(s_registryMutex);
            if (s_registry.contains(id)) {
                self = s_registry[id];
            }
        }

        if (self && self->m_task) {
            std::invoke(self->m_task);
            self->m_taskDone.put();
        }
    }

    auto Thread::nextRegistryId() -> ULONG
    {
        static std::atomic<ULONG> id{};
        return id.fetch_add(1);
    }

    auto Thread::nextThreadId() -> std::size_t
    {
        static std::atomic_size_t id{};
        return id.fetch_add(1);
    }
}
