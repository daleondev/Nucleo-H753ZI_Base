#include "thread_diagnostics.hpp"

#include <tx_api.h>
#include <tx_thread.h>

#include <cstdint>
#include <cstdio>

#if defined(HAL_COMPAT_LINUX)
#include <pthread.h>
#endif

namespace thread_diagnostics
{
    namespace
    {
        const char* threadName(const TX_THREAD* thread)
        {
            return thread->tx_thread_name != TX_NULL ? thread->tx_thread_name : "Unknown";
        }

        bool getStackMetrics(TX_THREAD* thread,
                             size_t& stack_size,
                             size_t& current_stack_usage,
                             size_t& peak_stack_usage)
        {
            const auto stack_end = reinterpret_cast<uintptr_t>(thread->tx_thread_stack_end);
            const auto current_stack_ptr = reinterpret_cast<uintptr_t>(thread->tx_thread_stack_ptr);
            const auto peak_stack_ptr = reinterpret_cast<uintptr_t>(thread->tx_thread_stack_highest_ptr);

            if (current_stack_ptr > stack_end || peak_stack_ptr > stack_end) {
                return false;
            }

            stack_size = static_cast<size_t>(thread->tx_thread_stack_size);
            current_stack_usage = static_cast<size_t>(stack_end - current_stack_ptr);
            peak_stack_usage = static_cast<size_t>(stack_end - peak_stack_ptr);
            return true;
        }

        void printThreadInfo(TX_THREAD* thread)
        {
            size_t stack_size = 0;
            size_t current_stack_usage = 0;
            size_t peak_stack_usage = 0;
            const bool has_stack_metrics =
              getStackMetrics(thread, stack_size, current_stack_usage, peak_stack_usage);

            if (has_stack_metrics) {
                std::printf("Thread %s (priority: %u, stack size: %zu, current stack usage: %zu, peak stack "
                            "usage: %zu)\n",
                            threadName(thread),
                            thread->tx_thread_priority,
                            stack_size,
                            current_stack_usage,
                            peak_stack_usage);
                return;
            }

            std::printf(
              "Thread %s (priority: %u, stack size: n/a, current stack usage: n/a, peak stack usage: n/a)\n",
              threadName(thread),
              thread->tx_thread_priority);
        }
    } // namespace

    void printAll()
    {
        std::printf("Threads info:\n");

        auto* first_thread = _tx_thread_created_ptr;
        if (first_thread == TX_NULL || _tx_thread_created_count == 0U) {
            std::printf("No threads created\n");
            std::fflush(stdout);
            return;
        }

        auto* current_thread = first_thread;
        do {
            printThreadInfo(current_thread);
            current_thread = current_thread->tx_thread_created_next;
        } while (current_thread != first_thread);

        std::fflush(stdout);
    }
} // namespace thread_diagnostics