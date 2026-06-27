#include "Thread.hpp"

namespace osal
{

    Thread::~Thread() { tx_thread_delete(&m_thread); }

    auto Thread::join() -> void
    {
        m_taskDone.get();
        tx_thread_terminate(&m_thread);
    }

    auto Thread::taskWrapper(ULONG context) -> VOID
    {
        auto self{ reinterpret_cast<Thread*>(static_cast<uintptr_t>(context)) };
        if (self && self->m_task) {
            self->m_task();
            self->m_taskDone.put();
        }
    }
}