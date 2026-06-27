#include "hal_compat/platform_hal.hpp"

#include <tx_api.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace
{
    constexpr size_t MAIN_THREAD_STACK_SIZE{ 4096 };
    constexpr UINT MAIN_THREAD_PRIO{ 15 };
    constexpr ULONG BLINK_PERIOD_MS{ 100 };

#if defined(ENABLE_LIBC_LOCK_TEST)
    constexpr size_t LIBC_LOCK_TEST_STACK_SIZE{ 2048 };
    constexpr UINT LIBC_LOCK_TEST_PRIO{ 14 };
    constexpr ULONG LIBC_LOCK_TEST_ITERATIONS{ 128 };
    constexpr ULONG LIBC_LOCK_TEST_THREAD_COUNT{ 2 };
#endif

    alignas(8) std::array<std::byte, MAIN_THREAD_STACK_SIZE> main_thread_stack{};
    CHAR main_thread_name[] = "Main Thread";
    TX_THREAD main_thread;

#if defined(ENABLE_LIBC_LOCK_TEST)
    alignas(8) std::array<std::byte, LIBC_LOCK_TEST_STACK_SIZE> libc_lock_test_stack_0{};
    alignas(8) std::array<std::byte, LIBC_LOCK_TEST_STACK_SIZE> libc_lock_test_stack_1{};
    CHAR libc_lock_test_name_0[] = "libc lock test 0";
    CHAR libc_lock_test_name_1[] = "libc lock test 1";
    CHAR libc_lock_test_done_name[] = "libc lock test done";
    TX_THREAD libc_lock_test_thread_0;
    TX_THREAD libc_lock_test_thread_1;
    TX_SEMAPHORE libc_lock_test_done;
#endif

    constexpr ULONG MillisecondsToTicks(ULONG milliseconds)
    {
        const auto ticks = (milliseconds * TX_TIMER_TICKS_PER_SECOND + 999UL) / 1000UL;
        return ticks == 0 ? 1UL : ticks;
    }

    void AssertTxCall(UINT status)
    {
        if (status != TX_SUCCESS) {
            Error_Handler();
        }
    }

    void ThreadStackErrorHandler(TX_THREAD* thread)
    {
        const auto* thread_name = thread != TX_NULL ? thread->tx_thread_name : "Unknown";
        std::printf("Thread %s stack overflow detected\n", thread_name);
        std::fflush(stdout);
        Error_Handler();
    }

#if defined(ENABLE_LIBC_LOCK_TEST)
    std::uint32_t HashBuffer(const char* buffer, std::size_t size)
    {
        std::uint32_t hash{ 2166136261U };

        for (std::size_t index{}; index < size; ++index) {
            hash ^= static_cast<std::uint8_t>(buffer[index]);
            hash *= 16777619U;
        }

        return hash;
    }

    void LibcLockTestWorker(ULONG worker_id)
    {
        for (ULONG iteration{}; iteration < LIBC_LOCK_TEST_ITERATIONS; ++iteration) {
            const std::size_t buffer_size{ 48U + ((worker_id * 7U + iteration) % 32U) };
            auto* const buffer{ static_cast<char*>(std::malloc(buffer_size)) };

            if (buffer == nullptr) {
                Error_Handler();
            }

            const int written{ std::snprintf(buffer,
                                             buffer_size,
                                             "worker=%lu iteration=%lu",
                                             static_cast<unsigned long>(worker_id),
                                             static_cast<unsigned long>(iteration)) };

            if (written < 0 || static_cast<std::size_t>(written) >= buffer_size
                || buffer[written] != '\0' || std::fflush(stdout) != 0) {
                Error_Handler();
            }

            const std::size_t used_size{ static_cast<std::size_t>(written) + 1U };
            const std::uint32_t expected_hash{ HashBuffer(buffer, used_size) };

            /* Keep this allocation live while the peer exercises malloc and
             * stdio. A one-tick time slice also permits preemption inside libc. */
            tx_thread_relinquish();

            if (HashBuffer(buffer, used_size) != expected_hash) {
                Error_Handler();
            }

            std::free(buffer);
        }

        if (std::printf("[libc-lock-test] worker %lu passed\n",
                        static_cast<unsigned long>(worker_id)) < 0
            || std::fflush(stdout) != 0) {
            Error_Handler();
        }

        AssertTxCall(tx_semaphore_put(&libc_lock_test_done));
    }
#endif

    void TxMain(ULONG)
    {
#if defined(ENABLE_LIBC_LOCK_TEST)
        for (ULONG worker{}; worker < LIBC_LOCK_TEST_THREAD_COUNT; ++worker) {
            AssertTxCall(tx_semaphore_get(&libc_lock_test_done, TX_WAIT_FOREVER));
        }

        if (std::printf("[libc-lock-test] all workers passed\n") < 0 || std::fflush(stdout) != 0) {
            Error_Handler();
        }
#endif

        const auto blink_period_ticks{ MillisecondsToTicks(BLINK_PERIOD_MS) };

        while (true) {
            AssertTxCall(
              static_cast<UINT>(BSP_LED_Toggle(LED_GREEN) == BSP_ERROR_NONE ? TX_SUCCESS : TX_NOT_DONE));
            AssertTxCall(
              static_cast<UINT>(BSP_LED_Toggle(LED_RED) == BSP_ERROR_NONE ? TX_SUCCESS : TX_NOT_DONE));
            tx_thread_sleep(blink_period_ticks);
        }
    }

} // namespace

extern "C" void tx_application_define(void* first_unused_memory)
{
    static_cast<void>(first_unused_memory);

    if (Platform_InitLibcLocks() != HAL_OK) {
        Error_Handler();
    }

    AssertTxCall(tx_thread_stack_error_notify(ThreadStackErrorHandler));
#if defined(ENABLE_LIBC_LOCK_TEST)
    AssertTxCall(tx_semaphore_create(&libc_lock_test_done, libc_lock_test_done_name, 0));
#endif

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

#if defined(ENABLE_LIBC_LOCK_TEST)
    AssertTxCall(tx_thread_create(&libc_lock_test_thread_0,
                                  libc_lock_test_name_0,
                                  LibcLockTestWorker,
                                  0,
                                  libc_lock_test_stack_0.data(),
                                  static_cast<ULONG>(libc_lock_test_stack_0.size()),
                                  LIBC_LOCK_TEST_PRIO,
                                  LIBC_LOCK_TEST_PRIO,
                                  1,
                                  TX_AUTO_START));
    AssertTxCall(tx_thread_create(&libc_lock_test_thread_1,
                                  libc_lock_test_name_1,
                                  LibcLockTestWorker,
                                  1,
                                  libc_lock_test_stack_1.data(),
                                  static_cast<ULONG>(libc_lock_test_stack_1.size()),
                                  LIBC_LOCK_TEST_PRIO,
                                  LIBC_LOCK_TEST_PRIO,
                                  1,
                                  TX_AUTO_START));
#endif
}

int main(void)
{
    MPU_Config_User();
    SCB_EnableICache();
    SCB_EnableDCache();

    HAL_Init();

    SystemClock_Config();

    MX_GPIO_Init();
    MX_ETH_Init();
    MX_RTC_Init();
    MX_TIM2_Init();
    MX_RNG_Init();
    MX_FDCAN1_Init();

    BSP_LED_Init(LED_GREEN);
    BSP_LED_Init(LED_RED);
    BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

    BspCOMInit.BaudRate = 115200;
    BspCOMInit.WordLength = COM_WORDLENGTH_8B;
    BspCOMInit.StopBits = COM_STOPBITS_1;
    BspCOMInit.Parity = COM_PARITY_NONE;
    BspCOMInit.HwFlowCtl = COM_HWCONTROL_NONE;

    if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE) {
        Error_Handler();
    }

    if (HAL_TIM_Base_Start(&htim2) != HAL_OK) {
        Error_Handler();
    }

    tx_kernel_enter();

    Error_Handler();
}
