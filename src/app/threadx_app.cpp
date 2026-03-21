#include "platform/platform.h"

#include <array>
#include <cstddef>

#include <tx_api.h>

namespace
{
    constexpr size_t MAIN_THREAD_STACK_SIZE{ 4096 };
    constexpr UINT MAIN_THREAD_PRIO{ 15 };
    constexpr ULONG BLINK_PERIOD_MS{ 100 };

    alignas(8) std::array<std::byte, MAIN_THREAD_STACK_SIZE> main_thread_stack{};
    CHAR main_thread_name[] = "Main Thread";
    TX_THREAD main_thread;

    constexpr ULONG MillisecondsToTicks(ULONG milliseconds)
    {
        const auto ticks = (milliseconds * TX_TIMER_TICKS_PER_SECOND + 999UL) / 1000UL;
        return ticks == 0 ? 1UL : ticks;
    }

    void AssertTxCall(UINT status)
    {
        if (status != TX_SUCCESS) {
            platform::FatalError();
        }
    }

    void ThreadStackErrorHandler(TX_THREAD* thread)
    {
        const auto* thread_name = thread != TX_NULL ? thread->tx_thread_name : "Unknown";
        platform::ReportStackOverflow(thread_name);
    }

    void TxMain(ULONG)
    {
        const auto blink_period_ticks{ MillisecondsToTicks(BLINK_PERIOD_MS) };

        while (true) {
            platform::ToggleDemoIndicators();
            tx_thread_sleep(blink_period_ticks);
        }
    }
}

extern "C" void tx_application_define(void* first_unused_memory)
{
    static_cast<void>(first_unused_memory);

    AssertTxCall(tx_thread_stack_error_notify(ThreadStackErrorHandler));
    AssertTxCall(tx_thread_create(&main_thread,
                                  main_thread_name,
                                  TxMain,
                                  0,
                                  main_thread_stack.data(),
                                  static_cast<ULONG>(main_thread_stack.size()),
                                  MAIN_THREAD_PRIO,
                                  MAIN_THREAD_PRIO,
                                  TX_NO_TIME_SLICE,
                                  TX_AUTO_START));
}