#pragma once

#include "Mutex.hpp"
#include "Semaphore.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <format>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <span>

#include <cassert> // tmp

namespace osal
{
    class Thread
    {
      public:
        template<typename Func, typename... Args>
        Thread(uint32_t prio, std::span<std::byte> stack, Func&& func, Args&&... args)
          : Thread(std::format("Thread_{}", nextThreadId()),
                   prio,
                   stack,
                   std::forward<Func>(func),
                   std::forward<Args>(args)...) {};

        template<typename Func, typename... Args>
        Thread(std::string_view name, uint32_t prio, std::span<std::byte> stack, Func&& func, Args&&... args)
          : m_registryId{ nextRegistryId() }
          , m_name{ name }
          , m_prio{ prio }
          , m_stack{ stack }
          , m_task{ [f = std::forward<Func>(func), ... a = std::forward<Args>(args)] {
              std::invoke(std::move(f), std::move(a)...);
          } }
        {
            {
                std::scoped_lock lock(s_registryMutex);
                s_registry[m_registryId] = this;
            }

            auto ret{ tx_thread_create(&m_thread,
                                       const_cast<CHAR*>(m_name.data()),
                                       run,
                                       m_registryId,
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

        Thread(Thread&& other) = delete;
        auto operator=(Thread&& other) -> Thread& = delete;

        auto join() -> void;

      private:
        static auto run(ULONG id) -> VOID;
        static auto nextRegistryId() -> ULONG;
        static auto nextThreadId() -> std::size_t;

        ULONG m_registryId;
        std::string m_name;
        uint32_t m_prio;
        std::span<std::byte> m_stack;
        std::function<void()> m_task;
        bool m_joined{ false };
        Semaphore m_taskDone{ std::format("{}_Done_Semaphore", m_name) };
        TX_THREAD m_thread{};

        inline static Mutex s_registryMutex{ "Thread_Registry_Mutex" };
        inline static std::map<ULONG, Thread*> s_registry;
    };
}
