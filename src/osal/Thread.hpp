#pragma once

#include "Semaphore.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <format>
#include <functional>
#include <optional>
#include <span>

#include <cassert> // tmp

namespace osal
{
    class Thread
    {
      private:
        inline static std::atomic_size_t s_id{ 0UZ };

      public:
        template<typename Func, typename... Args>
        Thread(int prio, std::span<std::byte> stack, Func&& func, Args&&... args)
          : Thread(std::format("Thread_{}", s_id++),
                   prio,
                   stack,
                   std::forward<Func>(func),
                   std::forward<Args>(args)...) {};

        template<typename Func, typename... Args>
        Thread(std::string_view name, int prio, std::span<std::byte> stack, Func&& func, Args&&... args)
          : m_name{ name }
          , m_prio{ prio }
          , m_stack{ stack }
          , m_task{ [f = std::forward<Func>(func), ... a = std::forward<Args>(args)] {
              std::invoke(std::move(f), std::move(a)...);
          } }
          , m_taskDone{ std::format("{}_Done_Semaphore", m_name) }
        {
            auto self{ reinterpret_cast<uintptr_t>(this) };
            auto ret{ tx_thread_create(&m_thread,
                                       const_cast<CHAR*>(m_name.data()),
                                       taskWrapper,
                                       static_cast<ULONG>(self),
                                       m_stack.data(),
                                       m_stack.size(),
                                       m_prio,
                                       m_prio,
                                       TX_NO_TIME_SLICE,
                                       TX_AUTO_START) };
            assert(ret == TX_SUCCESS);
        };

        ~Thread();

        Thread(const Thread&) = delete;
        auto operator=(const Thread&) -> Thread& = delete;

        Thread(Thread&& other);
        auto operator=(Thread&& other) -> Thread&;

        auto join() -> void;

      private:
        static auto taskWrapper(ULONG context) -> VOID;

        std::string m_name;
        int m_prio;
        std::span<std::byte> m_stack;
        std::function<void()> m_task;
        Semaphore m_taskDone;
        TX_THREAD m_thread;
    };
}
