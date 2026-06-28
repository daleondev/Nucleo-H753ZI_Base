#include "standard_library_self_test.hpp"

#include <algorithm>
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

    bool test_mutexes_and_condition_variables()
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
            if (condition.wait_for(lock, 1ms) != std::cv_status::timeout) {
                return false;
            }
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
        if (!timed_out) {
            return false;
        }

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
        return shared_timed_out;
    }

    bool test_semaphores()
    {
        std::binary_semaphore binary_semaphore{ 0 };
        std::thread binary_thread{ [&] { binary_semaphore.release(); } };
        binary_semaphore.acquire();
        binary_thread.join();

        std::counting_semaphore<4> counting_semaphore{ 0 };
        counting_semaphore.release(2);
        counting_semaphore.acquire();
        counting_semaphore.acquire();
        return !counting_semaphore.try_acquire() && !counting_semaphore.try_acquire_for(1ms);
    }

    bool test_atomics()
    {
        std::atomic<int> atomic_value{};
        std::thread atomic_thread{ [&] {
            atomic_value.wait(0);
            atomic_value.fetch_add(1);
        } };
        atomic_value.store(1);
        atomic_value.notify_one();
        atomic_thread.join();
        if (atomic_value.load() != 2) {
            return false;
        }

        std::atomic<std::uint64_t> wide_atomic{ WIDE_ATOMIC_INITIAL };
        if (wide_atomic.fetch_add(1U) != WIDE_ATOMIC_INITIAL ||
            wide_atomic.load() != WIDE_ATOMIC_INITIAL + 1U) {
            return false;
        }
        std::uint64_t wide_expected{ WIDE_ATOMIC_INITIAL + 1U };
        if (!wide_atomic.compare_exchange_strong(wide_expected, WIDE_ATOMIC_INITIAL + 2U) ||
            wide_atomic.exchange(WIDE_ATOMIC_INITIAL) != WIDE_ATOMIC_INITIAL + 2U) {
            return false;
        }
        wide_atomic.store(WIDE_ATOMIC_INITIAL + 1U);
        if (wide_atomic.load() != WIDE_ATOMIC_INITIAL + 1U) {
            return false;
        }

        std::atomic<AtomicRecord> record{ FIRST_RECORD };
        if (record.load() != FIRST_RECORD || record.exchange(SECOND_RECORD) != FIRST_RECORD) {
            return false;
        }
        AtomicRecord expected{ SECOND_RECORD };
        if (!record.compare_exchange_strong(expected, THIRD_RECORD) || record.load() != THIRD_RECORD) {
            return false;
        }
        record.store(FIRST_RECORD);
        if (record.load() != FIRST_RECORD) {
            return false;
        }

        std::atomic_flag flag;
        flag.test_and_set();
        std::thread flag_thread{ [&] {
            flag.wait(true);
            atomic_value.fetch_add(1);
        } };
        flag.clear();
        flag.notify_one();
        flag_thread.join();
        return atomic_value.load() == 3;
    }

    bool test_futures()
    {
        std::promise<int> promise;
        auto promised_value{ promise.get_future() };
        std::thread promise_thread{ [&] { promise.set_value(PROMISE_VALUE); } };
        if (promised_value.get() != PROMISE_VALUE) {
            return false;
        }
        promise_thread.join();

        std::promise<int> delayed_promise;
        auto delayed_value{ delayed_promise.get_future() };
        if (delayed_value.wait_for(1ms) != std::future_status::timeout) {
            return false;
        }
        delayed_promise.set_value(DELAYED_VALUE);
        if (delayed_value.wait_for(10ms) != std::future_status::ready ||
            delayed_value.get() != DELAYED_VALUE) {
            return false;
        }

        std::promise<int> exit_promise;
        auto exit_value{ exit_promise.get_future() };
        std::thread exit_promise_thread{ [promise = std::move(exit_promise)]() mutable {
            promise.set_value_at_thread_exit(EXIT_PROMISE_VALUE);
        } };
        exit_promise_thread.join();
        if (exit_value.get() != EXIT_PROMISE_VALUE) {
            return false;
        }

        auto asynchronous_value{ std::async(std::launch::async, [] { return ASYNC_VALUE; }) };
        auto deferred_value{ std::async(std::launch::deferred, [] { return DEFERRED_VALUE; }) };
        if (asynchronous_value.get() != ASYNC_VALUE ||
            deferred_value.wait_for(0ms) != std::future_status::deferred ||
            deferred_value.get() != DEFERRED_VALUE) {
            return false;
        }

        std::packaged_task<int()> task{ [] { return TASK_VALUE; } };
        auto task_value{ task.get_future() };
        std::thread task_thread{ std::move(task) };
        task_thread.join();
        if (task_value.get() != TASK_VALUE) {
            return false;
        }

        std::promise<int> shared_promise;
        std::shared_future<int> shared_value{ shared_promise.get_future() };
        shared_promise.set_value(SHARED_VALUE);
        return shared_value.get() == SHARED_VALUE && shared_value.get() == SHARED_VALUE;
    }

    bool test_stop_tokens()
    {
        std::atomic<bool> stopped{};
        std::jthread interruptible_thread{ [&](const std::stop_token& token) {
            while (!token.stop_requested()) {
                std::this_thread::yield();
            }
            stopped.store(true);
        } };
        interruptible_thread.request_stop();
        interruptible_thread.join();
        if (!stopped.load()) {
            return false;
        }

        std::condition_variable_any interruptible_condition;
        std::mutex interruptible_mutex;
        bool wait_was_stopped{};
        std::jthread condition_any_thread{ [&](std::stop_token token) {
            std::unique_lock lock{ interruptible_mutex };
            wait_was_stopped =
              !interruptible_condition.wait(lock, std::move(token), [] { return false; });
        } };
        condition_any_thread.request_stop();
        condition_any_thread.join();
        return wait_was_stopped;
    }

    bool test_barrier_and_latch()
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
        if (barrier_count.load() != 2) {
            return false;
        }

        std::latch completion{ 2 };
        std::jthread latch_thread_1{ [&] { completion.count_down(); } };
        std::jthread latch_thread_2{ [&] { completion.count_down(); } };
        completion.wait();
        return true;
    }

    bool test_thread_exit_and_once()
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

        std::once_flag retry_once;
        int retry_count{};
        bool exception_observed{};
        try {
            std::call_once(retry_once, [&] {
                ++retry_count;
                throw std::runtime_error{ "retry" };
            });
        }
        catch (const std::runtime_error&) {
            exception_observed = true;
        }
        std::call_once(retry_once, [&] { ++retry_count; });

        return once_count == 1 && retry_count == 2 && exception_observed;
    }
}

bool run_standard_library_self_test()
{
    using Test = bool (*)();
    constexpr std::array tests{
        static_cast<Test>(test_mutexes_and_condition_variables),
        static_cast<Test>(test_semaphores),
        static_cast<Test>(test_atomics),
        static_cast<Test>(test_futures),
        static_cast<Test>(test_stop_tokens),
        static_cast<Test>(test_barrier_and_latch),
        static_cast<Test>(test_thread_exit_and_once),
    };

    return std::ranges::all_of(tests, [](Test test) { return test(); });
}
