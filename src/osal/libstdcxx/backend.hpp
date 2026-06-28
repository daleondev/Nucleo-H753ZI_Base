#pragma once

#include <tx_api.h>

#include <cstddef>
#include <cstdint>
#include <ctime>

namespace osal
{
    [[nodiscard]] UINT initialize() noexcept;

    namespace detail
    {
        struct MutexHandle
        {
            void* implementation{};
        };

        struct RecursiveMutexHandle
        {
            void* implementation{};
        };

        struct ConditionHandle
        {
            void* implementation{};
        };

        struct OnceHandle
        {
            void* implementation{};
        };

        struct SemaphoreHandle
        {
            void* implementation{};
            unsigned int initial_count{};
        };

        using ThreadHandle = TX_THREAD*;
        using TimePoint = timespec;

        [[nodiscard]] bool active() noexcept;
        [[nodiscard]] bool in_thread_context() noexcept;

        int thread_create(ThreadHandle* thread, void* (*entry)(void*), void* argument) noexcept;
        int thread_join(ThreadHandle thread, void** result) noexcept;
        int thread_detach(ThreadHandle thread) noexcept;
        [[nodiscard]] ThreadHandle thread_self() noexcept;
        int thread_yield() noexcept;

        void mutex_init(MutexHandle* mutex) noexcept;
        void recursive_mutex_init(RecursiveMutexHandle* mutex) noexcept;
        int mutex_destroy(MutexHandle* mutex) noexcept;
        int recursive_mutex_destroy(RecursiveMutexHandle* mutex) noexcept;
        int mutex_lock(MutexHandle* mutex) noexcept;
        int recursive_mutex_lock(RecursiveMutexHandle* mutex) noexcept;
        int mutex_try_lock(MutexHandle* mutex) noexcept;
        int recursive_mutex_try_lock(RecursiveMutexHandle* mutex) noexcept;
        int mutex_timed_lock(MutexHandle* mutex, const TimePoint* deadline) noexcept;
        int recursive_mutex_timed_lock(RecursiveMutexHandle* mutex, const TimePoint* deadline) noexcept;
        int mutex_unlock(MutexHandle* mutex) noexcept;
        int recursive_mutex_unlock(RecursiveMutexHandle* mutex) noexcept;

        void condition_init(ConditionHandle* condition) noexcept;
        int condition_destroy(ConditionHandle* condition) noexcept;
        int condition_wait(ConditionHandle* condition, MutexHandle* mutex) noexcept;
        int condition_timed_wait(ConditionHandle* condition,
                                 MutexHandle* mutex,
                                 const TimePoint* deadline) noexcept;
        int condition_signal(ConditionHandle* condition) noexcept;
        int condition_broadcast(ConditionHandle* condition) noexcept;

        int once(OnceHandle* once_control, void (*function)());

        int semaphore_init(SemaphoreHandle* semaphore, unsigned int value) noexcept;
        int semaphore_destroy(SemaphoreHandle* semaphore) noexcept;
        int semaphore_wait(SemaphoreHandle* semaphore) noexcept;
        int semaphore_try_wait(SemaphoreHandle* semaphore) noexcept;
        int semaphore_timed_wait(SemaphoreHandle* semaphore, const TimePoint* deadline) noexcept;
        int semaphore_post(SemaphoreHandle* semaphore) noexcept;

        [[nodiscard]] std::uint64_t steady_ticks() noexcept;
        [[nodiscard]] std::uint64_t extend_tick_counter(std::uint64_t& epoch,
                                                        std::uint32_t& previous,
                                                        std::uint32_t current) noexcept;
        [[nodiscard]] std::int64_t system_time_nanoseconds() noexcept;
        [[nodiscard]] ULONG duration_to_ticks(std::uint64_t nanoseconds) noexcept;
        void sleep_for(std::uint64_t nanoseconds) noexcept;

        [[noreturn]] void fatal_error(const char* operation, UINT status) noexcept;
    }
}