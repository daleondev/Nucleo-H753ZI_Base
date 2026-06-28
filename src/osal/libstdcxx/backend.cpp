#include "backend.hpp"

#include <algorithm>
#if defined(__linux__)
#include <chrono>
#endif
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <new>

// ThreadX requires mutable C control blocks, raw stack storage, C callbacks,
// and explicit lifetime management at this implementation boundary.
// NOLINTBEGIN(bugprone-easily-swappable-parameters,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-missing-std-forward,cppcoreguidelines-owning-memory,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-type-const-cast,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-vararg,modernize-avoid-c-arrays,readability-magic-numbers,readability-math-missing-parentheses,readability-named-parameter)

#ifndef OSAL_STD_THREAD_STACK_SIZE
#define OSAL_STD_THREAD_STACK_SIZE 4096U
#endif

#ifndef OSAL_STD_THREAD_PRIORITY
#define OSAL_STD_THREAD_PRIORITY 16U
#endif

namespace osal
{
    namespace detail
    {
        namespace
        {
            constexpr std::size_t STACK_ALIGNMENT{ 8U };
            constexpr std::size_t REAPER_STACK_SIZE{ 2048U };
            constexpr UINT REAPER_PRIORITY{ 31U };
            constexpr std::size_t CLEANUP_QUEUE_DEPTH{ 16U };
            constexpr ULONG ROLLOVER_SAMPLE_TICKS{ 0x7FFFFFFFUL };
            constexpr ULONG MAX_FINITE_WAIT{ TX_WAIT_FOREVER - 1UL };
            constexpr std::uint64_t NANOSECONDS_PER_SECOND{ 1'000'000'000ULL };

            struct MutexImplementation
            {
                TX_MUTEX mutex{};
            };

            struct Waiter
            {
                Waiter* previous{};
                Waiter* next{};
                TX_SEMAPHORE semaphore{};
                bool linked{};
            };

            struct ConditionImplementation
            {
                TX_MUTEX mutex{};
                Waiter* first{};
                Waiter* last{};
            };

            struct OnceImplementation
            {
                TX_MUTEX mutex{};
                unsigned int state{};
            };

            struct SemaphoreImplementation
            {
                TX_SEMAPHORE semaphore{};
            };

            struct ThreadControl
            {
                TX_THREAD thread{};
                ThreadControl* registry_next{};
                ULONG registry_id{};
                void* stack{};
                void* (*entry)(void*){};
                void* argument{};
                TX_MUTEX lifecycle_mutex{};
                TX_SEMAPHORE completion{};
                TX_SEMAPHORE* cleanup_ack{};
                bool detached{};
                bool finished{};
                bool cleanup_queued{};
                char name[24]{};
            };

            static_assert(offsetof(ThreadControl, thread) == 0U);
            static_assert(OSAL_STD_THREAD_PRIORITY < 32U);

            TX_MUTEX initialization_mutex;
            TX_MUTEX clock_mutex;
            TX_QUEUE cleanup_queue;
            TX_THREAD reaper_thread;
            TX_TIMER rollover_timer;
            alignas(STACK_ALIGNMENT) UCHAR reaper_stack[REAPER_STACK_SIZE];
            ULONG cleanup_queue_storage[CLEANUP_QUEUE_DEPTH];
            CHAR initialization_mutex_name[] = "std init";
            CHAR clock_mutex_name[] = "std clock";
            CHAR cleanup_queue_name[] = "std cleanup";
            CHAR reaper_thread_name[] = "std reaper";
            CHAR rollover_timer_name[] = "std clock wrap";
            CHAR mutex_name[] = "std mutex";
            CHAR condition_mutex_name[] = "std condition";
            CHAR waiter_semaphore_name[] = "std cv waiter";
            CHAR once_mutex_name[] = "std once";
            CHAR semaphore_name[] = "std semaphore";
            CHAR completion_name[] = "std completion";
            CHAR lifecycle_mutex_name[] = "std lifecycle";
            bool initialized{};
            std::uint32_t next_thread_number{};
            ULONG next_registry_id{ 1U };
            ThreadControl* registry_head{};
            std::uint32_t last_tick{};
            std::uint64_t tick_epoch{};

            [[nodiscard]] bool interrupt_context() noexcept
            {
#if defined(__arm__) || defined(__thumb__)
                std::uint32_t ipsr{};
                asm volatile("mrs %0, ipsr" : "=r"(ipsr));
                return ipsr != 0U;
#else
                return false;
#endif
            }

            [[nodiscard]] int require_thread_context() noexcept
            {
                if (!initialized) {
                    return EAGAIN;
                }
                if (interrupt_context() || tx_thread_identify() == TX_NULL) {
                    return EPERM;
                }
                return 0;
            }

            void raw_mutex_get(TX_MUTEX* mutex)
            {
                const UINT status{ tx_mutex_get(mutex, TX_WAIT_FOREVER) };
                if (status != TX_SUCCESS) {
                    fatal_error("tx_mutex_get", status);
                }
            }

            void raw_mutex_put(TX_MUTEX* mutex)
            {
                const UINT status{ tx_mutex_put(mutex) };
                if (status != TX_SUCCESS) {
                    fatal_error("tx_mutex_put", status);
                }
            }

            template<typename Handle>
            [[nodiscard]] MutexImplementation* ensure_mutex(Handle* handle) noexcept
            {
                if (handle == nullptr || require_thread_context() != 0) {
                    return nullptr;
                }
                if (handle->implementation != nullptr) {
                    return static_cast<MutexImplementation*>(handle->implementation);
                }

                raw_mutex_get(&initialization_mutex);
                if (handle->implementation == nullptr) {
                    auto* implementation{ new (std::nothrow) MutexImplementation{} };
                    if (implementation != nullptr) {
                        if (tx_mutex_create(&implementation->mutex, mutex_name, TX_INHERIT) == TX_SUCCESS) {
                            handle->implementation = implementation;
                        }
                        else {
                            delete implementation;
                        }
                    }
                }
                auto* result{ static_cast<MutexImplementation*>(handle->implementation) };
                raw_mutex_put(&initialization_mutex);
                return result;
            }

            template<typename Handle>
            int destroy_mutex(Handle* handle) noexcept
            {
                if (handle == nullptr) {
                    return EINVAL;
                }
                auto* implementation{ static_cast<MutexImplementation*>(handle->implementation) };
                if (implementation == nullptr) {
                    return 0;
                }
                const UINT status{ tx_mutex_delete(&implementation->mutex) };
                if (status != TX_SUCCESS) {
                    return status == TX_DELETE_ERROR ? EBUSY : EINVAL;
                }
                handle->implementation = nullptr;
                delete implementation;
                return 0;
            }

            template<typename Handle>
            int lock_mutex(Handle* handle, bool recursive, ULONG wait_option) noexcept
            {
                const int context_status{ require_thread_context() };
                if (context_status != 0) {
                    return context_status;
                }
                auto* implementation{ ensure_mutex(handle) };
                if (implementation == nullptr) {
                    return ENOMEM;
                }

                if (!recursive && implementation->mutex.tx_mutex_owner == tx_thread_identify()) {
                    return wait_option == TX_NO_WAIT ? EBUSY : EDEADLK;
                }

                const UINT status{ tx_mutex_get(&implementation->mutex, wait_option) };
                if (status == TX_SUCCESS) {
                    return 0;
                }
                if (status == TX_NOT_AVAILABLE) {
                    return EBUSY;
                }
                if (status == TX_NO_INSTANCE) {
                    return ETIMEDOUT;
                }
                return EINVAL;
            }

            template<typename Handle>
            int unlock_mutex(Handle* handle) noexcept
            {
                const int context_status{ require_thread_context() };
                if (context_status != 0) {
                    return context_status;
                }
                if (handle == nullptr || handle->implementation == nullptr) {
                    return EPERM;
                }
                auto* implementation{ static_cast<MutexImplementation*>(handle->implementation) };
                return tx_mutex_put(&implementation->mutex) == TX_SUCCESS ? 0 : EPERM;
            }

            [[nodiscard]] std::int64_t timespec_to_nanoseconds(const TimePoint& time) noexcept
            {
                constexpr auto maximum_seconds{ std::numeric_limits<std::int64_t>::max() /
                                                static_cast<std::int64_t>(NANOSECONDS_PER_SECOND) };
                if (time.tv_sec >= maximum_seconds) {
                    return std::numeric_limits<std::int64_t>::max();
                }
                if (time.tv_sec < 0) {
                    return -1;
                }
                return static_cast<std::int64_t>(time.tv_sec) *
                         static_cast<std::int64_t>(NANOSECONDS_PER_SECOND) +
                       std::clamp<std::int64_t>(time.tv_nsec, 0, NANOSECONDS_PER_SECOND - 1U);
            }

            [[nodiscard]] ULONG deadline_wait_ticks(const TimePoint* deadline) noexcept
            {
                if (deadline == nullptr) {
                    return TX_NO_WAIT;
                }
                const std::int64_t deadline_ns{ timespec_to_nanoseconds(*deadline) };
                const std::int64_t now_ns{ system_time_nanoseconds() };
                if (deadline_ns <= now_ns) {
                    return TX_NO_WAIT;
                }
                const auto remaining{ static_cast<std::uint64_t>(deadline_ns - now_ns) };
                return std::min(duration_to_ticks(remaining), MAX_FINITE_WAIT);
            }

            template<typename WaitFunction>
            int wait_until(const TimePoint* deadline, WaitFunction&& wait_function) noexcept
            {
                while (true) {
                    const ULONG wait_ticks{ deadline_wait_ticks(deadline) };
                    if (wait_ticks == TX_NO_WAIT) {
                        return wait_function(TX_NO_WAIT) == TX_SUCCESS ? 0 : ETIMEDOUT;
                    }
                    const UINT status{ wait_function(wait_ticks) };
                    if (status == TX_SUCCESS) {
                        return 0;
                    }
                    if (status != TX_NO_INSTANCE && status != TX_NOT_AVAILABLE) {
                        return EINVAL;
                    }
                    if (wait_ticks < MAX_FINITE_WAIT) {
                        return ETIMEDOUT;
                    }
                }
            }

            [[nodiscard]] ConditionImplementation* ensure_condition(ConditionHandle* handle) noexcept
            {
                if (handle == nullptr || require_thread_context() != 0) {
                    return nullptr;
                }
                if (handle->implementation != nullptr) {
                    return static_cast<ConditionImplementation*>(handle->implementation);
                }

                raw_mutex_get(&initialization_mutex);
                if (handle->implementation == nullptr) {
                    auto* implementation{ new (std::nothrow) ConditionImplementation{} };
                    if (implementation != nullptr) {
                        if (tx_mutex_create(&implementation->mutex, condition_mutex_name, TX_INHERIT) ==
                            TX_SUCCESS) {
                            handle->implementation = implementation;
                        }
                        else {
                            delete implementation;
                        }
                    }
                }
                auto* result{ static_cast<ConditionImplementation*>(handle->implementation) };
                raw_mutex_put(&initialization_mutex);
                return result;
            }

            void append_waiter(ConditionImplementation* condition, Waiter* waiter) noexcept
            {
                waiter->previous = condition->last;
                waiter->next = nullptr;
                waiter->linked = true;
                if (condition->last != nullptr) {
                    condition->last->next = waiter;
                }
                else {
                    condition->first = waiter;
                }
                condition->last = waiter;
            }

            void remove_waiter(ConditionImplementation* condition, Waiter* waiter) noexcept
            {
                if (!waiter->linked) {
                    return;
                }
                if (waiter->previous != nullptr) {
                    waiter->previous->next = waiter->next;
                }
                else {
                    condition->first = waiter->next;
                }
                if (waiter->next != nullptr) {
                    waiter->next->previous = waiter->previous;
                }
                else {
                    condition->last = waiter->previous;
                }
                waiter->linked = false;
            }

            [[nodiscard]] SemaphoreImplementation* ensure_semaphore(SemaphoreHandle* handle) noexcept
            {
                if (handle == nullptr || require_thread_context() != 0) {
                    return nullptr;
                }
                if (handle->implementation != nullptr) {
                    return static_cast<SemaphoreImplementation*>(handle->implementation);
                }

                raw_mutex_get(&initialization_mutex);
                if (handle->implementation == nullptr) {
                    auto* implementation{ new (std::nothrow) SemaphoreImplementation{} };
                    if (implementation != nullptr) {
                        if (tx_semaphore_create(&implementation->semaphore,
                                                semaphore_name,
                                                static_cast<ULONG>(handle->initial_count)) == TX_SUCCESS) {
                            handle->implementation = implementation;
                        }
                        else {
                            delete implementation;
                        }
                    }
                }
                auto* result{ static_cast<SemaphoreImplementation*>(handle->implementation) };
                raw_mutex_put(&initialization_mutex);
                return result;
            }

            void register_thread(ThreadControl* control)
            {
                raw_mutex_get(&initialization_mutex);
                control->registry_id = next_registry_id++;
                if (next_registry_id == 0U) {
                    next_registry_id = 1U;
                }
                control->registry_next = registry_head;
                registry_head = control;
                raw_mutex_put(&initialization_mutex);
            }

            void unregister_thread(ThreadControl* control)
            {
                raw_mutex_get(&initialization_mutex);
                ThreadControl** current{ &registry_head };
                while (*current != nullptr && *current != control) {
                    current = &(*current)->registry_next;
                }
                if (*current == control) {
                    *current = control->registry_next;
                }
                raw_mutex_put(&initialization_mutex);
            }

            [[nodiscard]] ThreadControl* find_thread(ULONG registry_id)
            {
                raw_mutex_get(&initialization_mutex);
                ThreadControl* current{ registry_head };
                while (current != nullptr && current->registry_id != registry_id) {
                    current = current->registry_next;
                }
                raw_mutex_put(&initialization_mutex);
                return current;
            }

            void enqueue_cleanup(ThreadControl* control)
            {
                const ULONG message{ control->registry_id };
                const UINT status{ tx_queue_send(
                  &cleanup_queue, const_cast<ULONG*>(&message), TX_WAIT_FOREVER) };
                if (status != TX_SUCCESS) {
                    fatal_error("tx_queue_send", status);
                }
            }

            void thread_entry(ULONG parameter)
            {
                auto* control{ find_thread(parameter) };
                if (control == nullptr) {
                    fatal_error("thread registry", TX_PTR_ERROR);
                }
                static_cast<void>(control->entry(control->argument));

                raw_mutex_get(&control->lifecycle_mutex);
                control->finished = true;
                const bool enqueue{ control->detached && !control->cleanup_queued };
                if (enqueue) {
                    control->cleanup_queued = true;
                }
                raw_mutex_put(&control->lifecycle_mutex);

                if (tx_semaphore_put(&control->completion) != TX_SUCCESS) {
                    fatal_error("tx_semaphore_put", TX_SEMAPHORE_ERROR);
                }
                if (enqueue) {
                    enqueue_cleanup(control);
                }
            }

            void reaper_entry(ULONG)
            {
                while (true) {
                    ULONG message{};
                    const UINT receive_status{ tx_queue_receive(&cleanup_queue, &message, TX_WAIT_FOREVER) };
                    if (receive_status != TX_SUCCESS) {
                        fatal_error("tx_queue_receive", receive_status);
                    }
                    auto* control{ find_thread(message) };
                    if (control == nullptr) {
                        fatal_error("cleanup registry", TX_PTR_ERROR);
                    }

                    UINT state{};
                    while (true) {
                        const UINT info_status{ tx_thread_info_get(&control->thread,
                                                                   nullptr,
                                                                   &state,
                                                                   nullptr,
                                                                   nullptr,
                                                                   nullptr,
                                                                   nullptr,
                                                                   nullptr,
                                                                   nullptr) };
                        if (info_status != TX_SUCCESS) {
                            fatal_error("tx_thread_info_get", info_status);
                        }
                        if (state == TX_COMPLETED || state == TX_TERMINATED) {
                            break;
                        }
                        tx_thread_sleep(1U);
                    }

                    TX_SEMAPHORE* const cleanup_ack{ control->cleanup_ack };
                    void* const stack{ control->stack };
                    if (tx_thread_delete(&control->thread) != TX_SUCCESS ||
                        tx_mutex_delete(&control->lifecycle_mutex) != TX_SUCCESS ||
                        tx_semaphore_delete(&control->completion) != TX_SUCCESS) {
                        fatal_error("thread cleanup", TX_DELETE_ERROR);
                    }
                    unregister_thread(control);
                    delete control;
                    ::operator delete[](stack);
                    if (cleanup_ack != nullptr && tx_semaphore_put(cleanup_ack) != TX_SUCCESS) {
                        fatal_error("cleanup acknowledgement", TX_SEMAPHORE_ERROR);
                    }
                }
            }

            void rollover_timer_entry(ULONG) { static_cast<void>(steady_ticks()); }
        }

        bool active() noexcept { return initialized; }

        bool in_thread_context() noexcept
        {
            return initialized && !interrupt_context() && tx_thread_identify() != TX_NULL;
        }

        int thread_create(ThreadHandle* thread, void* (*entry)(void*), void* argument) noexcept
        {
            const int context_status{ require_thread_context() };
            if (context_status != 0) {
                return context_status;
            }
            if (thread == nullptr || entry == nullptr) {
                return EINVAL;
            }

            auto* control{ new (std::nothrow) ThreadControl{} };
            if (control == nullptr) {
                return EAGAIN;
            }
            control->stack = ::operator new[](OSAL_STD_THREAD_STACK_SIZE, std::nothrow);
            if (control->stack == nullptr) {
                delete control;
                return EAGAIN;
            }
            control->entry = entry;
            control->argument = argument;

            const ULONG old_posture{ tx_interrupt_control(TX_INT_DISABLE) };
            const std::uint32_t number{ next_thread_number++ };
            static_cast<void>(tx_interrupt_control(old_posture));
            static_cast<void>(std::snprintf(
              control->name, sizeof(control->name), "std::thread %lu", static_cast<unsigned long>(number)));

            if (tx_mutex_create(&control->lifecycle_mutex, lifecycle_mutex_name, TX_INHERIT) != TX_SUCCESS) {
                ::operator delete[](control->stack);
                delete control;
                return EAGAIN;
            }
            if (tx_semaphore_create(&control->completion, completion_name, 0U) != TX_SUCCESS) {
                static_cast<void>(tx_mutex_delete(&control->lifecycle_mutex));
                ::operator delete[](control->stack);
                delete control;
                return EAGAIN;
            }

            register_thread(control);

            const UINT status{ tx_thread_create(&control->thread,
                                                control->name,
                                                thread_entry,
                                                control->registry_id,
                                                control->stack,
                                                OSAL_STD_THREAD_STACK_SIZE,
                                                OSAL_STD_THREAD_PRIORITY,
                                                OSAL_STD_THREAD_PRIORITY,
                                                TX_NO_TIME_SLICE,
                                                TX_AUTO_START) };
            if (status != TX_SUCCESS) {
                unregister_thread(control);
                static_cast<void>(tx_semaphore_delete(&control->completion));
                static_cast<void>(tx_mutex_delete(&control->lifecycle_mutex));
                ::operator delete[](control->stack);
                delete control;
                return EAGAIN;
            }
            *thread = &control->thread;
            return 0;
        }

        int thread_join(ThreadHandle thread, void** result) noexcept
        {
            const int context_status{ require_thread_context() };
            if (context_status != 0) {
                return context_status;
            }
            if (thread == nullptr) {
                return EINVAL;
            }
            if (thread == tx_thread_identify()) {
                return EDEADLK;
            }
            auto* control{ reinterpret_cast<ThreadControl*>(thread) };
            if (tx_semaphore_get(&control->completion, TX_WAIT_FOREVER) != TX_SUCCESS) {
                return EINVAL;
            }

            TX_SEMAPHORE acknowledgement{};
            CHAR acknowledgement_name[] = "std join ack";
            if (tx_semaphore_create(&acknowledgement, acknowledgement_name, 0U) != TX_SUCCESS) {
                return EAGAIN;
            }

            raw_mutex_get(&control->lifecycle_mutex);
            if (control->detached || control->cleanup_queued) {
                raw_mutex_put(&control->lifecycle_mutex);
                static_cast<void>(tx_semaphore_delete(&acknowledgement));
                return EINVAL;
            }
            control->cleanup_ack = &acknowledgement;
            control->cleanup_queued = true;
            raw_mutex_put(&control->lifecycle_mutex);
            enqueue_cleanup(control);

            if (tx_semaphore_get(&acknowledgement, TX_WAIT_FOREVER) != TX_SUCCESS) {
                fatal_error("join acknowledgement", TX_SEMAPHORE_ERROR);
            }
            if (tx_semaphore_delete(&acknowledgement) != TX_SUCCESS) {
                fatal_error("join acknowledgement delete", TX_DELETE_ERROR);
            }
            if (result != nullptr) {
                *result = nullptr;
            }
            return 0;
        }

        int thread_detach(ThreadHandle thread) noexcept
        {
            const int context_status{ require_thread_context() };
            if (context_status != 0) {
                return context_status;
            }
            if (thread == nullptr) {
                return EINVAL;
            }
            auto* control{ reinterpret_cast<ThreadControl*>(thread) };
            raw_mutex_get(&control->lifecycle_mutex);
            if (control->detached || control->cleanup_queued) {
                raw_mutex_put(&control->lifecycle_mutex);
                return EINVAL;
            }
            control->detached = true;
            const bool enqueue{ control->finished };
            if (enqueue) {
                control->cleanup_queued = true;
            }
            raw_mutex_put(&control->lifecycle_mutex);
            if (enqueue) {
                enqueue_cleanup(control);
            }
            return 0;
        }

        ThreadHandle thread_self() noexcept { return tx_thread_identify(); }

        int thread_yield() noexcept
        {
            if (require_thread_context() != 0) {
                return EPERM;
            }
            tx_thread_relinquish();
            return 0;
        }

        void mutex_init(MutexHandle* mutex) noexcept
        {
            if (mutex != nullptr) {
                mutex->implementation = nullptr;
            }
        }

        void recursive_mutex_init(RecursiveMutexHandle* mutex) noexcept
        {
            if (mutex != nullptr) {
                mutex->implementation = nullptr;
            }
        }

        int mutex_destroy(MutexHandle* mutex) noexcept { return destroy_mutex(mutex); }
        int recursive_mutex_destroy(RecursiveMutexHandle* mutex) noexcept { return destroy_mutex(mutex); }
        int mutex_lock(MutexHandle* mutex) noexcept { return lock_mutex(mutex, false, TX_WAIT_FOREVER); }
        int recursive_mutex_lock(RecursiveMutexHandle* mutex) noexcept
        {
            return lock_mutex(mutex, true, TX_WAIT_FOREVER);
        }
        int mutex_try_lock(MutexHandle* mutex) noexcept { return lock_mutex(mutex, false, TX_NO_WAIT); }
        int recursive_mutex_try_lock(RecursiveMutexHandle* mutex) noexcept
        {
            return lock_mutex(mutex, true, TX_NO_WAIT);
        }

        int mutex_timed_lock(MutexHandle* mutex, const TimePoint* deadline) noexcept
        {
            const int context_status{ require_thread_context() };
            if (context_status != 0) {
                return context_status;
            }
            auto* implementation{ ensure_mutex(mutex) };
            if (implementation == nullptr) {
                return ENOMEM;
            }
            if (implementation->mutex.tx_mutex_owner == tx_thread_identify()) {
                return EDEADLK;
            }
            return wait_until(deadline,
                              [&](ULONG ticks) { return tx_mutex_get(&implementation->mutex, ticks); });
        }

        int recursive_mutex_timed_lock(RecursiveMutexHandle* mutex, const TimePoint* deadline) noexcept
        {
            const int context_status{ require_thread_context() };
            if (context_status != 0) {
                return context_status;
            }
            auto* implementation{ ensure_mutex(mutex) };
            if (implementation == nullptr) {
                return ENOMEM;
            }
            return wait_until(deadline,
                              [&](ULONG ticks) { return tx_mutex_get(&implementation->mutex, ticks); });
        }

        int mutex_unlock(MutexHandle* mutex) noexcept { return unlock_mutex(mutex); }
        int recursive_mutex_unlock(RecursiveMutexHandle* mutex) noexcept { return unlock_mutex(mutex); }

        void condition_init(ConditionHandle* condition) noexcept
        {
            if (condition != nullptr) {
                condition->implementation = nullptr;
            }
        }

        int condition_destroy(ConditionHandle* condition) noexcept
        {
            if (condition == nullptr) {
                return EINVAL;
            }
            auto* implementation{ static_cast<ConditionImplementation*>(condition->implementation) };
            if (implementation == nullptr) {
                return 0;
            }
            raw_mutex_get(&implementation->mutex);
            const bool busy{ implementation->first != nullptr };
            raw_mutex_put(&implementation->mutex);
            if (busy) {
                return EBUSY;
            }
            if (tx_mutex_delete(&implementation->mutex) != TX_SUCCESS) {
                return EBUSY;
            }
            condition->implementation = nullptr;
            delete implementation;
            return 0;
        }

        namespace
        {
            int condition_wait_common(ConditionHandle* condition,
                                      MutexHandle* mutex,
                                      const TimePoint* deadline) noexcept
            {
                const int context_status{ require_thread_context() };
                if (context_status != 0) {
                    return context_status;
                }
                auto* condition_implementation{ ensure_condition(condition) };
                if (condition_implementation == nullptr || mutex == nullptr ||
                    mutex->implementation == nullptr) {
                    return EINVAL;
                }

                Waiter waiter{};
                if (tx_semaphore_create(&waiter.semaphore, waiter_semaphore_name, 0U) != TX_SUCCESS) {
                    return EAGAIN;
                }

                raw_mutex_get(&condition_implementation->mutex);
                append_waiter(condition_implementation, &waiter);
                const int unlock_status{ mutex_unlock(mutex) };
                raw_mutex_put(&condition_implementation->mutex);
                if (unlock_status != 0) {
                    raw_mutex_get(&condition_implementation->mutex);
                    remove_waiter(condition_implementation, &waiter);
                    raw_mutex_put(&condition_implementation->mutex);
                    static_cast<void>(tx_semaphore_delete(&waiter.semaphore));
                    return unlock_status;
                }

                int wait_status{};
                if (deadline == nullptr) {
                    wait_status =
                      tx_semaphore_get(&waiter.semaphore, TX_WAIT_FOREVER) == TX_SUCCESS ? 0 : EINVAL;
                }
                else {
                    wait_status = wait_until(
                      deadline, [&](ULONG ticks) { return tx_semaphore_get(&waiter.semaphore, ticks); });
                }

                if (wait_status == ETIMEDOUT) {
                    raw_mutex_get(&condition_implementation->mutex);
                    if (waiter.linked) {
                        remove_waiter(condition_implementation, &waiter);
                    }
                    else {
                        wait_status =
                          tx_semaphore_get(&waiter.semaphore, TX_NO_WAIT) == TX_SUCCESS ? 0 : EINVAL;
                    }
                    raw_mutex_put(&condition_implementation->mutex);
                }

                if (tx_semaphore_delete(&waiter.semaphore) != TX_SUCCESS) {
                    fatal_error("condition waiter delete", TX_DELETE_ERROR);
                }
                const int lock_status{ mutex_lock(mutex) };
                return lock_status == 0 ? wait_status : lock_status;
            }
        }

        int condition_wait(ConditionHandle* condition, MutexHandle* mutex) noexcept
        {
            return condition_wait_common(condition, mutex, nullptr);
        }

        int condition_timed_wait(ConditionHandle* condition,
                                 MutexHandle* mutex,
                                 const TimePoint* deadline) noexcept
        {
            return condition_wait_common(condition, mutex, deadline);
        }

        int condition_signal(ConditionHandle* condition) noexcept
        {
            const int context_status{ require_thread_context() };
            if (context_status != 0) {
                return context_status;
            }
            if (condition == nullptr || condition->implementation == nullptr) {
                return 0;
            }
            auto* implementation{ static_cast<ConditionImplementation*>(condition->implementation) };
            raw_mutex_get(&implementation->mutex);
            Waiter* const waiter{ implementation->first };
            if (waiter != nullptr) {
                remove_waiter(implementation, waiter);
                if (tx_semaphore_put(&waiter->semaphore) != TX_SUCCESS) {
                    fatal_error("condition signal", TX_SEMAPHORE_ERROR);
                }
            }
            raw_mutex_put(&implementation->mutex);
            return 0;
        }

        int condition_broadcast(ConditionHandle* condition) noexcept
        {
            const int context_status{ require_thread_context() };
            if (context_status != 0) {
                return context_status;
            }
            if (condition == nullptr || condition->implementation == nullptr) {
                return 0;
            }
            auto* implementation{ static_cast<ConditionImplementation*>(condition->implementation) };
            raw_mutex_get(&implementation->mutex);
            while (implementation->first != nullptr) {
                Waiter* const waiter{ implementation->first };
                remove_waiter(implementation, waiter);
                if (tx_semaphore_put(&waiter->semaphore) != TX_SUCCESS) {
                    fatal_error("condition broadcast", TX_SEMAPHORE_ERROR);
                }
            }
            raw_mutex_put(&implementation->mutex);
            return 0;
        }

        int once(OnceHandle* once_control, void (*function)())
        {
            const int context_status{ require_thread_context() };
            if (context_status != 0 || once_control == nullptr || function == nullptr) {
                return context_status != 0 ? context_status : EINVAL;
            }

            if (once_control->implementation == nullptr) {
                raw_mutex_get(&initialization_mutex);
                if (once_control->implementation == nullptr) {
                    auto* implementation{ new (std::nothrow) OnceImplementation{} };
                    if (implementation != nullptr &&
                        tx_mutex_create(&implementation->mutex, once_mutex_name, TX_INHERIT) == TX_SUCCESS) {
                        once_control->implementation = implementation;
                    }
                    else {
                        delete implementation;
                    }
                }
                raw_mutex_put(&initialization_mutex);
            }
            auto* implementation{ static_cast<OnceImplementation*>(once_control->implementation) };
            if (implementation == nullptr) {
                return ENOMEM;
            }

            while (true) {
                raw_mutex_get(&implementation->mutex);
                if (implementation->state == 2U) {
                    raw_mutex_put(&implementation->mutex);
                    return 0;
                }
                if (implementation->state == 0U) {
                    implementation->state = 1U;
                    raw_mutex_put(&implementation->mutex);
                    try {
                        function();
                        raw_mutex_get(&implementation->mutex);
                        implementation->state = 2U;
                        raw_mutex_put(&implementation->mutex);
                        return 0;
                    } catch (...) {
                        raw_mutex_get(&implementation->mutex);
                        implementation->state = 0U;
                        raw_mutex_put(&implementation->mutex);
                        throw;
                    }
                }
                raw_mutex_put(&implementation->mutex);
                tx_thread_sleep(1U);
            }
        }

        int semaphore_init(SemaphoreHandle* semaphore, unsigned int value) noexcept
        {
            if (semaphore == nullptr) {
                return EINVAL;
            }
            semaphore->implementation = nullptr;
            semaphore->initial_count = value;
            return 0;
        }

        int semaphore_destroy(SemaphoreHandle* semaphore) noexcept
        {
            if (semaphore == nullptr) {
                return EINVAL;
            }
            auto* implementation{ static_cast<SemaphoreImplementation*>(semaphore->implementation) };
            if (implementation == nullptr) {
                return 0;
            }
            if (tx_semaphore_delete(&implementation->semaphore) != TX_SUCCESS) {
                return EBUSY;
            }
            semaphore->implementation = nullptr;
            delete implementation;
            return 0;
        }

        int semaphore_wait(SemaphoreHandle* semaphore) noexcept
        {
            const int context_status{ require_thread_context() };
            if (context_status != 0) {
                return context_status;
            }
            auto* implementation{ ensure_semaphore(semaphore) };
            if (implementation == nullptr) {
                return ENOMEM;
            }
            return tx_semaphore_get(&implementation->semaphore, TX_WAIT_FOREVER) == TX_SUCCESS ? 0 : EINVAL;
        }

        int semaphore_try_wait(SemaphoreHandle* semaphore) noexcept
        {
            const int context_status{ require_thread_context() };
            if (context_status != 0) {
                return context_status;
            }
            auto* implementation{ ensure_semaphore(semaphore) };
            if (implementation == nullptr) {
                return ENOMEM;
            }
            const UINT status{ tx_semaphore_get(&implementation->semaphore, TX_NO_WAIT) };
            return status == TX_SUCCESS ? 0 : EAGAIN;
        }

        int semaphore_timed_wait(SemaphoreHandle* semaphore, const TimePoint* deadline) noexcept
        {
            const int context_status{ require_thread_context() };
            if (context_status != 0) {
                return context_status;
            }
            auto* implementation{ ensure_semaphore(semaphore) };
            if (implementation == nullptr) {
                return ENOMEM;
            }
            return wait_until(
              deadline, [&](ULONG ticks) { return tx_semaphore_get(&implementation->semaphore, ticks); });
        }

        int semaphore_post(SemaphoreHandle* semaphore) noexcept
        {
            const int context_status{ require_thread_context() };
            if (context_status != 0) {
                return context_status;
            }
            auto* implementation{ ensure_semaphore(semaphore) };
            if (implementation == nullptr) {
                return ENOMEM;
            }
            return tx_semaphore_put(&implementation->semaphore) == TX_SUCCESS ? 0 : EOVERFLOW;
        }

        std::uint64_t steady_ticks() noexcept
        {
            const ULONG old_posture{ tx_interrupt_control(TX_INT_DISABLE) };
            const auto current_tick{ static_cast<std::uint32_t>(tx_time_get()) };
            const std::uint64_t result{ extend_tick_counter(tick_epoch, last_tick, current_tick) };
            static_cast<void>(tx_interrupt_control(old_posture));
            return result;
        }

        std::uint64_t extend_tick_counter(std::uint64_t& epoch,
                                          std::uint32_t& previous,
                                          std::uint32_t current) noexcept
        {
            if (current < previous) {
                epoch += (1ULL << 32U);
            }
            previous = current;
            return epoch | current;
        }

        std::int64_t system_time_nanoseconds() noexcept
        {
#if defined(__linux__)
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
              .count();
#else
            const std::uint64_t ticks{ steady_ticks() };
            const std::uint64_t seconds{ ticks / TX_TIMER_TICKS_PER_SECOND };
            const std::uint64_t remainder{ ticks % TX_TIMER_TICKS_PER_SECOND };
            constexpr auto maximum{ static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) };
            if (seconds > maximum / NANOSECONDS_PER_SECOND) {
                return std::numeric_limits<std::int64_t>::max();
            }
            return static_cast<std::int64_t>(seconds * NANOSECONDS_PER_SECOND +
                                             remainder * NANOSECONDS_PER_SECOND /
                                               TX_TIMER_TICKS_PER_SECOND);
#endif
        }

        ULONG duration_to_ticks(std::uint64_t nanoseconds) noexcept
        {
            if (nanoseconds == 0U) {
                return TX_NO_WAIT;
            }
            constexpr std::uint64_t tick_rate{ TX_TIMER_TICKS_PER_SECOND };
            const std::uint64_t whole_seconds{ nanoseconds / NANOSECONDS_PER_SECOND };
            const std::uint64_t remainder{ nanoseconds % NANOSECONDS_PER_SECOND };
            if (whole_seconds > std::numeric_limits<ULONG>::max() / tick_rate) {
                return MAX_FINITE_WAIT;
            }
            const std::uint64_t ticks{ whole_seconds * tick_rate +
                                       (remainder * tick_rate + NANOSECONDS_PER_SECOND - 1U) /
                                         NANOSECONDS_PER_SECOND };
            return static_cast<ULONG>(std::min<std::uint64_t>(ticks, MAX_FINITE_WAIT));
        }

        void sleep_for(std::uint64_t nanoseconds) noexcept
        {
            if (require_thread_context() != 0 || nanoseconds == 0U) {
                return;
            }
            const std::uint64_t whole_seconds{ nanoseconds / NANOSECONDS_PER_SECOND };
            const std::uint64_t remainder{ nanoseconds % NANOSECONDS_PER_SECOND };
            std::uint64_t ticks{ whole_seconds * TX_TIMER_TICKS_PER_SECOND +
                                 (remainder * TX_TIMER_TICKS_PER_SECOND + NANOSECONDS_PER_SECOND - 1U) /
                                   NANOSECONDS_PER_SECOND };
            while (ticks != 0U) {
                const ULONG chunk{ static_cast<ULONG>(std::min<std::uint64_t>(ticks, MAX_FINITE_WAIT)) };
                if (tx_thread_sleep(chunk) != TX_SUCCESS) {
                    fatal_error("tx_thread_sleep", TX_WAIT_ERROR);
                }
                ticks -= chunk;
            }
        }

        [[noreturn]] void fatal_error(const char* operation, UINT status) noexcept
        {
            static_cast<void>(operation);
            static_cast<void>(status);
            std::terminate();
        }
    }

    UINT initialize() noexcept
    {
        if (detail::initialized) {
            return TX_SUCCESS;
        }

        UINT status{ tx_mutex_create(
          &detail::initialization_mutex, detail::initialization_mutex_name, TX_INHERIT) };
        if (status != TX_SUCCESS) {
            return status;
        }
        status = tx_mutex_create(&detail::clock_mutex, detail::clock_mutex_name, TX_INHERIT);
        if (status != TX_SUCCESS) {
            static_cast<void>(tx_mutex_delete(&detail::initialization_mutex));
            return status;
        }
        status = tx_queue_create(&detail::cleanup_queue,
                                 detail::cleanup_queue_name,
                                 TX_1_ULONG,
                                 detail::cleanup_queue_storage,
                                 sizeof(detail::cleanup_queue_storage));
        if (status != TX_SUCCESS) {
            static_cast<void>(tx_mutex_delete(&detail::clock_mutex));
            static_cast<void>(tx_mutex_delete(&detail::initialization_mutex));
            return status;
        }
        status = tx_timer_create(&detail::rollover_timer,
                                 detail::rollover_timer_name,
                                 detail::rollover_timer_entry,
                                 0U,
                                 detail::ROLLOVER_SAMPLE_TICKS,
                                 detail::ROLLOVER_SAMPLE_TICKS,
                                 TX_AUTO_ACTIVATE);
        if (status != TX_SUCCESS) {
            static_cast<void>(tx_queue_delete(&detail::cleanup_queue));
            static_cast<void>(tx_mutex_delete(&detail::clock_mutex));
            static_cast<void>(tx_mutex_delete(&detail::initialization_mutex));
            return status;
        }
        status = tx_thread_create(&detail::reaper_thread,
                                  detail::reaper_thread_name,
                                  detail::reaper_entry,
                                  0U,
                                  detail::reaper_stack,
                                  sizeof(detail::reaper_stack),
                                  detail::REAPER_PRIORITY,
                                  detail::REAPER_PRIORITY,
                                  TX_NO_TIME_SLICE,
                                  TX_AUTO_START);
        if (status != TX_SUCCESS) {
            static_cast<void>(tx_timer_delete(&detail::rollover_timer));
            static_cast<void>(tx_queue_delete(&detail::cleanup_queue));
            static_cast<void>(tx_mutex_delete(&detail::clock_mutex));
            static_cast<void>(tx_mutex_delete(&detail::initialization_mutex));
            return status;
        }

        detail::last_tick = static_cast<std::uint32_t>(tx_time_get());
        detail::tick_epoch = 0U;
        detail::initialized = true;
        return TX_SUCCESS;
    }
}

extern "C" void osal_libstdcxx_initialize(void)
{
    if (osal::initialize() != TX_SUCCESS) {
        std::terminate();
    }
}

// NOLINTEND(bugprone-easily-swappable-parameters,cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-missing-std-forward,cppcoreguidelines-owning-memory,cppcoreguidelines-pro-bounds-array-to-pointer-decay,cppcoreguidelines-pro-type-const-cast,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-vararg,modernize-avoid-c-arrays,readability-magic-numbers,readability-math-missing-parentheses,readability-named-parameter)
