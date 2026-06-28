#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <latch>
#include <mutex>
#include <semaphore>
#include <shared_mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

using namespace std::chrono_literals;

namespace
{
    constexpr std::uint64_t WIDE_ATOMIC_INITIAL{ 0x1'0000'0000ULL };
    constexpr int PROMISE_VALUE{ 42 };
    constexpr int ASYNC_VALUE{ 43 };
    constexpr int TASK_VALUE{ 44 };
    constexpr int EXIT_PROMISE_VALUE{ 45 };
    constexpr int DEFERRED_VALUE{ 46 };
    constexpr int SHARED_VALUE{ 47 };
    constexpr int DELAYED_VALUE{ 48 };
    constexpr std::uint32_t STATIC_VALUE{ 0x51A71C42U };

    struct AtomicRecord
    {
        std::uint32_t first;
        std::uint32_t second;
        std::uint32_t third;

        constexpr bool operator==(const AtomicRecord&) const = default;
    };

    constexpr AtomicRecord FIRST_RECORD{ .first = 1U, .second = 2U, .third = 3U };
    constexpr AtomicRecord SECOND_RECORD{ .first = 4U, .second = 5U, .third = 6U };
    constexpr AtomicRecord THIRD_RECORD{ .first = 7U, .second = 8U, .third = 9U };

    std::atomic_uint static_access_count{};
    std::atomic_uint static_constructor_count{};
    std::atomic_bool static_constructor_synchronized{};

    class ThreadsafeStatic
    {
      public:
        ThreadsafeStatic()
        {
            static_constructor_count.fetch_add(1U, std::memory_order_relaxed);
            const auto deadline{ std::chrono::steady_clock::now() + 100ms };
            while (static_access_count.load(std::memory_order_acquire) < 2U &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            static_constructor_synchronized.store(
              static_access_count.load(std::memory_order_acquire) == 2U,
              std::memory_order_release);
        }

        [[nodiscard]] auto value() const noexcept -> std::uint32_t { return STATIC_VALUE; }
    };

    auto threadsafe_static() -> ThreadsafeStatic&
    {
        static ThreadsafeStatic instance;
        return instance;
    }
}

TEST(RuntimeLibstdcxx, MutexesAndConditionVariables)
{
    std::mutex mutex;
    std::condition_variable condition;
    bool ready{};
    std::thread condition_thread{ [&] {
        {
            std::lock_guard lock{ mutex };
            ready = true;
        }
        condition.notify_one();
    } };
    {
        std::unique_lock lock{ mutex };
        condition.wait(lock, [&] { return ready; });
    }
    condition_thread.join();

    {
        std::unique_lock lock{ mutex };
        EXPECT_EQ(condition.wait_for(lock, 1ms), std::cv_status::timeout);
    }

    std::recursive_mutex recursive_mutex;
    recursive_mutex.lock();
    recursive_mutex.lock();
    recursive_mutex.unlock();
    recursive_mutex.unlock();

    std::timed_mutex timed_mutex;
    timed_mutex.lock();
    bool timed_out{};
    std::thread timed_thread{ [&] { timed_out = !timed_mutex.try_lock_for(1ms); } };
    timed_thread.join();
    timed_mutex.unlock();
    EXPECT_TRUE(timed_out);

    std::shared_mutex shared_mutex;
    shared_mutex.lock_shared();
    shared_mutex.unlock_shared();
    shared_mutex.lock();
    shared_mutex.unlock();

    std::shared_timed_mutex shared_timed_mutex;
    shared_timed_mutex.lock();
    bool shared_timed_out{};
    std::thread shared_timed_thread{
        [&] { shared_timed_out = !shared_timed_mutex.try_lock_shared_for(1ms); }
    };
    shared_timed_thread.join();
    shared_timed_mutex.unlock();
    EXPECT_TRUE(shared_timed_out);
}

TEST(RuntimeLibstdcxx, Semaphores)
{
    std::binary_semaphore binary_semaphore{ 0 };
    std::thread binary_thread{ [&] { binary_semaphore.release(); } };
    binary_semaphore.acquire();
    binary_thread.join();

    std::counting_semaphore<4> counting_semaphore{ 0 };
    counting_semaphore.release(2);
    counting_semaphore.acquire();
    counting_semaphore.acquire();
    EXPECT_FALSE(counting_semaphore.try_acquire());
    EXPECT_FALSE(counting_semaphore.try_acquire_for(1ms));
}

TEST(RuntimeLibstdcxx, Atomics)
{
    std::atomic<int> atomic_value{};
    std::thread atomic_thread{ [&] {
        atomic_value.wait(0);
        atomic_value.fetch_add(1);
    } };
    atomic_value.store(1);
    atomic_value.notify_one();
    atomic_thread.join();
    EXPECT_EQ(atomic_value.load(), 2);

    std::atomic<std::uint64_t> wide_atomic{ WIDE_ATOMIC_INITIAL };
    EXPECT_EQ(wide_atomic.fetch_add(1U), WIDE_ATOMIC_INITIAL);
    EXPECT_EQ(wide_atomic.load(), WIDE_ATOMIC_INITIAL + 1U);
    std::uint64_t wide_expected{ WIDE_ATOMIC_INITIAL + 1U };
    EXPECT_TRUE(wide_atomic.compare_exchange_strong(wide_expected, WIDE_ATOMIC_INITIAL + 2U));
    EXPECT_EQ(wide_atomic.exchange(WIDE_ATOMIC_INITIAL), WIDE_ATOMIC_INITIAL + 2U);
    wide_atomic.store(WIDE_ATOMIC_INITIAL + 1U);
    EXPECT_EQ(wide_atomic.load(), WIDE_ATOMIC_INITIAL + 1U);

    std::atomic<AtomicRecord> record{ FIRST_RECORD };
    EXPECT_EQ(record.load(), FIRST_RECORD);
    EXPECT_EQ(record.exchange(SECOND_RECORD), FIRST_RECORD);
    AtomicRecord expected{ SECOND_RECORD };
    EXPECT_TRUE(record.compare_exchange_strong(expected, THIRD_RECORD));
    EXPECT_EQ(record.load(), THIRD_RECORD);
    record.store(FIRST_RECORD);
    EXPECT_EQ(record.load(), FIRST_RECORD);

    std::atomic_flag flag;
    flag.test_and_set();
    std::thread flag_thread{ [&] {
        flag.wait(true);
        atomic_value.fetch_add(1);
    } };
    flag.clear();
    flag.notify_one();
    flag_thread.join();
    EXPECT_EQ(atomic_value.load(), 3);
}

TEST(RuntimeLibstdcxx, FuturesPromisesAndTasks)
{
    std::promise<int> promise;
    auto promised_value{ promise.get_future() };
    std::thread promise_thread{ [&] { promise.set_value(PROMISE_VALUE); } };
    EXPECT_EQ(promised_value.get(), PROMISE_VALUE);
    promise_thread.join();

    std::promise<int> delayed_promise;
    auto delayed_value{ delayed_promise.get_future() };
    EXPECT_EQ(delayed_value.wait_for(1ms), std::future_status::timeout);
    delayed_promise.set_value(DELAYED_VALUE);
    EXPECT_EQ(delayed_value.wait_for(10ms), std::future_status::ready);
    EXPECT_EQ(delayed_value.get(), DELAYED_VALUE);

    std::promise<int> exit_promise;
    auto exit_value{ exit_promise.get_future() };
    std::thread exit_promise_thread{ [promise_at_exit = std::move(exit_promise)]() mutable {
        promise_at_exit.set_value_at_thread_exit(EXIT_PROMISE_VALUE);
    } };
    exit_promise_thread.join();
    EXPECT_EQ(exit_value.get(), EXIT_PROMISE_VALUE);

    auto asynchronous_value{ std::async(std::launch::async, [] { return ASYNC_VALUE; }) };
    auto deferred_value{ std::async(std::launch::deferred, [] { return DEFERRED_VALUE; }) };
    EXPECT_EQ(asynchronous_value.get(), ASYNC_VALUE);
    EXPECT_EQ(deferred_value.wait_for(0ms), std::future_status::deferred);
    EXPECT_EQ(deferred_value.get(), DEFERRED_VALUE);

    std::packaged_task<int()> task{ [] { return TASK_VALUE; } };
    auto task_value{ task.get_future() };
    std::thread task_thread{ std::move(task) };
    task_thread.join();
    EXPECT_EQ(task_value.get(), TASK_VALUE);

    std::promise<int> shared_promise;
    std::shared_future<int> shared_value{ shared_promise.get_future() };
    shared_promise.set_value(SHARED_VALUE);
    EXPECT_EQ(shared_value.get(), SHARED_VALUE);
    EXPECT_EQ(shared_value.get(), SHARED_VALUE);
}

TEST(RuntimeLibstdcxx, StopTokensAndJthreads)
{
    std::atomic<bool> stopped{};
    std::jthread interruptible_thread{ [&](const std::stop_token& token) {
        while (!token.stop_requested()) {
            std::this_thread::yield();
        }
        stopped.store(true);
    } };
    EXPECT_TRUE(interruptible_thread.request_stop());
    interruptible_thread.join();
    EXPECT_TRUE(stopped.load());

    std::condition_variable_any interruptible_condition;
    std::mutex interruptible_mutex;
    bool wait_was_stopped{};
    std::jthread condition_any_thread{ [&](std::stop_token token) {
        std::unique_lock lock{ interruptible_mutex };
        wait_was_stopped =
          !interruptible_condition.wait(lock, std::move(token), [] { return false; });
    } };
    EXPECT_TRUE(condition_any_thread.request_stop());
    condition_any_thread.join();
    EXPECT_TRUE(wait_was_stopped);
}

TEST(RuntimeLibstdcxx, BarriersAndLatches)
{
    std::atomic<int> barrier_count{};
    std::barrier synchronization_point{ 3 };
    std::jthread barrier_thread_1{ [&] {
        barrier_count.fetch_add(1);
        synchronization_point.arrive_and_wait();
    } };
    std::jthread barrier_thread_2{ [&] {
        barrier_count.fetch_add(1);
        synchronization_point.arrive_and_wait();
    } };
    synchronization_point.arrive_and_wait();
    barrier_thread_1.join();
    barrier_thread_2.join();
    EXPECT_EQ(barrier_count.load(), 2);

    std::latch completion{ 2 };
    std::jthread latch_thread_1{ [&] { completion.count_down(); } };
    std::jthread latch_thread_2{ [&] { completion.count_down(); } };
    completion.wait();
}

TEST(RuntimeLibstdcxx, ThreadExitAndCallOnce)
{
    std::mutex exit_mutex;
    std::condition_variable exit_condition;
    bool exiting{};
    std::thread notifying_thread{ [&] {
        std::unique_lock lock{ exit_mutex };
        exiting = true;
        std::notify_all_at_thread_exit(exit_condition, std::move(lock));
    } };
    {
        std::unique_lock lock{ exit_mutex };
        exit_condition.wait(lock, [&] { return exiting; });
    }
    notifying_thread.join();

    std::once_flag once;
    int once_count{};
    std::thread once_thread_1{ [&] { std::call_once(once, [&] { ++once_count; }); } };
    std::thread once_thread_2{ [&] { std::call_once(once, [&] { ++once_count; }); } };
    once_thread_1.join();
    once_thread_2.join();
    EXPECT_EQ(once_count, 1);

    std::once_flag retry_once;
    int retry_count{};
    EXPECT_THROW(std::call_once(retry_once,
                               [&] {
                                   ++retry_count;
                                   throw std::runtime_error{ "retry" };
                               }),
                 std::runtime_error);
    std::call_once(retry_once, [&] { ++retry_count; });
    EXPECT_EQ(retry_count, 2);
}

TEST(RuntimeLibstdcxx, ContendedStaticInitialization)
{
    std::array<ThreadsafeStatic*, 2U> instances{};
    std::array<std::thread, 2U> workers;

    for (std::size_t index{}; index < workers.size(); ++index) {
        workers[index] = std::thread{ [&, index] {
            static_access_count.fetch_add(1U, std::memory_order_release);
            instances[index] = &threadsafe_static();
        } };
    }
    for (auto& worker : workers) {
        worker.join();
    }

    EXPECT_TRUE(static_constructor_synchronized.load(std::memory_order_acquire));
    EXPECT_EQ(static_constructor_count.load(std::memory_order_acquire), 1U);
    ASSERT_NE(instances[0], nullptr);
    EXPECT_EQ(instances[0], instances[1]);
    EXPECT_EQ(instances[0]->value(), STATIC_VALUE);
}

TEST(RuntimeLibstdcxx, ClocksAndSleep)
{
    const auto steady_before{ std::chrono::steady_clock::now() };
    const auto system_before{ std::chrono::system_clock::now() };
    std::this_thread::sleep_for(2ms);
    const auto steady_after{ std::chrono::steady_clock::now() };
    const auto system_after{ std::chrono::system_clock::now() };

    EXPECT_GE(steady_after - steady_before, 2ms);
    EXPECT_GT(system_before.time_since_epoch(), 400'000h);
    EXPECT_GT(system_after, system_before);
    static_assert(std::is_same_v<std::chrono::high_resolution_clock, std::chrono::system_clock>);
}
