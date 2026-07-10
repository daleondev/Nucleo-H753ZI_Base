#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <barrier>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <latch>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <regex>
#include <semaphore>
#include <span>
#include <sstream>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <cerrno>
#include <ctime>
#include <unistd.h>

extern "C" int _getentropy(void* buffer, std::size_t length);
extern "C" int _write(int file, const char* buffer, int length);

extern "C"
{
    // These stable values are convenient acceptance points for a debugger or
    // an automated probe when the serial connection is unavailable.
    std::uint32_t runtime_hardware_self_test_status{};
    std::uint32_t runtime_hardware_self_test_phase{};
}

namespace
{
    using namespace std::chrono_literals;

    constexpr std::uint32_t PASS_STATUS{ 0x600D600DU };
    constexpr std::uint32_t FAIL_STATUS{ 0xBAD00000U };
    constexpr std::uint64_t WIDE_ATOMIC_INITIAL{ 0x1'0000'0000ULL };

    struct StartupTlsProbe
    {
        StartupTlsProbe() noexcept
          : self{ this }
        {
        }

        StartupTlsProbe* self;
        std::uint32_t value{ 0x71A5U };
    };

    thread_local StartupTlsProbe startup_tls_probe;
    StartupTlsProbe* startup_tls_address{};

    struct StartupTlsCapture
    {
        StartupTlsCapture() noexcept
        {
            startup_tls_address = &startup_tls_probe;
            startup_tls_probe.value = 0xC0FFEEU;
        }
    } startup_tls_capture;

    std::atomic_uint worker_tls_destructor_count{};

    struct WorkerTlsProbe
    {
        ~WorkerTlsProbe() { worker_tls_destructor_count.fetch_add(1U, std::memory_order_relaxed); }
        std::uint32_t value{};
    };

    thread_local WorkerTlsProbe worker_tls_probe;

    struct ExitOrderState
    {
        std::mutex mutex;
        std::condition_variable condition;
        std::atomic_bool notification_registered{};
        std::atomic_bool destructor_finished{};
    };

    struct ExitOrderTlsProbe
    {
        ~ExitOrderTlsProbe()
        {
            if (state != nullptr) {
                std::this_thread::sleep_for(5ms);
                state->destructor_finished.store(true, std::memory_order_release);
            }
        }

        ExitOrderState* state{};
    };

    thread_local ExitOrderTlsProbe exit_order_tls_probe;

    struct AtomicRecord
    {
        std::uint32_t first;
        std::uint32_t second;
        std::uint32_t third;

        constexpr bool operator==(const AtomicRecord&) const = default;
    };

    constexpr AtomicRecord FIRST_RECORD{ .first = 1U, .second = 2U, .third = 3U };
    constexpr AtomicRecord SECOND_RECORD{ .first = 4U, .second = 5U, .third = 6U };

    template<typename... Arguments>
    void log(const char* pattern, Arguments... arguments)
    {
        static_cast<void>(std::printf(pattern, arguments...));
        static_cast<void>(std::putchar('\n'));
        static_cast<void>(std::fflush(stdout));
    }

    [[nodiscard]] bool test_library_surface()
    {
        const std::string formatted{ std::format("{}:{:04x}", "runtime", 42) };
        if (formatted != "runtime:002a") {
            return false;
        }

        std::vector<int> values{ 5, 1, 4, 2, 3 };
        std::ranges::sort(values);
        if (!std::ranges::equal(values, std::array{ 1, 2, 3, 4, 5 }) ||
            std::accumulate(values.begin(), values.end(), 0) != 15) {
            return false;
        }

        const std::expected<int, std::string> result{ 42 };
        const std::optional<std::string> optional{ "optional" };
        const std::variant<int, std::string> variant{ std::in_place_type<std::string>, "variant" };
        const std::any any{ std::uint32_t{ 99U } };
        if (result.value() != 42 || optional.value() != "optional" ||
            std::get<std::string>(variant) != "variant" || std::any_cast<std::uint32_t>(any) != 99U) {
            return false;
        }

        std::array<char, 16U> encoded{};
        const auto encode_result{ std::to_chars(encoded.data(), encoded.data() + encoded.size(), 12345) };
        int decoded{};
        const auto decode_result{ std::from_chars(encoded.data(), encode_result.ptr, decoded) };
        if (encode_result.ec != std::errc{} || decode_result.ec != std::errc{} || decoded != 12345) {
            return false;
        }

        std::istringstream stream{ "17 alpha" };
        int number{};
        std::string word;
        stream >> number >> word;
        return stream && number == 17 && word == "alpha" &&
               std::regex_match("threadx-753", std::regex{ R"(threadx-[0-9]+)" });
    }

    [[nodiscard]] bool test_threads_and_synchronization()
    {
        std::mutex mutex;
        std::condition_variable condition;
        bool ready{};
        std::thread producer{ [&] {
            {
                const std::lock_guard lock{ mutex };
                ready = true;
            }
            condition.notify_one();
        } };
        {
            std::unique_lock lock{ mutex };
            condition.wait(lock, [&] { return ready; });
        }
        producer.join();

        std::timed_mutex timed_mutex;
        timed_mutex.lock();
        bool timed_out{};
        std::thread timed_waiter{ [&] { timed_out = !timed_mutex.try_lock_for(2ms); } };
        timed_waiter.join();
        timed_mutex.unlock();
        if (!timed_out) {
            return false;
        }

        std::binary_semaphore semaphore{ 0 };
        std::thread releaser{ [&] { semaphore.release(); } };
        semaphore.acquire();
        releaser.join();

        std::atomic_uint arrivals{};
        std::barrier barrier{ 3 };
        std::jthread first{ [&] {
            arrivals.fetch_add(1U, std::memory_order_relaxed);
            barrier.arrive_and_wait();
        } };
        std::jthread second{ [&] {
            arrivals.fetch_add(1U, std::memory_order_relaxed);
            barrier.arrive_and_wait();
        } };
        barrier.arrive_and_wait();
        first.join();
        second.join();

        std::once_flag outer;
        std::once_flag inner;
        unsigned int once_count{};
        std::call_once(outer, [&] {
            ++once_count;
            std::call_once(inner, [&] { ++once_count; });
        });
        std::call_once(outer, [&] { ++once_count; });

        std::atomic_bool stopped{};
        std::jthread stoppable{ [&](std::stop_token token) {
            while (!token.stop_requested()) {
                std::this_thread::yield();
            }
            stopped.store(true, std::memory_order_release);
        } };
        const bool stop_requested{ stoppable.request_stop() };
        stoppable.join();

        return arrivals.load(std::memory_order_relaxed) == 2U && once_count == 2U && stop_requested &&
               stopped.load(std::memory_order_acquire) && std::thread::hardware_concurrency() == 1U;
    }

    [[nodiscard]] bool test_atomics_futures_and_exceptions()
    {
        std::atomic<int> value{};
        std::thread waiter{ [&] {
            value.wait(0);
            value.fetch_add(1, std::memory_order_acq_rel);
        } };
        value.store(1, std::memory_order_release);
        value.notify_one();
        waiter.join();
        if (value.load(std::memory_order_acquire) != 2) {
            return false;
        }

        std::atomic<std::uint64_t> wide{ WIDE_ATOMIC_INITIAL };
        if (wide.fetch_add(1U) != WIDE_ATOMIC_INITIAL || wide.exchange(7U) != WIDE_ATOMIC_INITIAL + 1U) {
            return false;
        }
        std::uint64_t expected{ 7U };
        if (!wide.compare_exchange_strong(expected, 9U) || wide.load() != 9U) {
            return false;
        }

        std::atomic<AtomicRecord> record{ FIRST_RECORD };
        if (record.exchange(SECOND_RECORD) != FIRST_RECORD || record.load() != SECOND_RECORD) {
            return false;
        }

        std::promise<int> promise;
        std::future<int> future{ promise.get_future() };
        std::thread promise_thread{ [promise = std::move(promise)]() mutable {
            try {
                throw 41;
            } catch (...) {
                promise.set_value(std::any_cast<int>(std::any{ 42 }));
            }
        } };
        const bool future_ready{ future.wait_for(100ms) == std::future_status::ready };
        const int future_value{ future_ready ? future.get() : 0 };
        promise_thread.join();

        auto asynchronous{ std::async(std::launch::async, [] { return std::make_tuple(1, 2, 3); }) };
        return future_ready && future_value == 42 && asynchronous.get() == std::make_tuple(1, 2, 3);
    }

    [[nodiscard]] bool test_tls_and_thread_exit_order()
    {
        if (startup_tls_address == nullptr || startup_tls_address != &startup_tls_probe ||
            startup_tls_probe.self != &startup_tls_probe || startup_tls_probe.value != 0xC0FFEEU) {
            return false;
        }

        const unsigned int destructors_before{ worker_tls_destructor_count.load(std::memory_order_relaxed) };
        std::array<WorkerTlsProbe*, 2U> addresses{};
        std::array<std::thread, 2U> workers;
        for (std::size_t index{}; index < workers.size(); ++index) {
            workers[index] = std::thread{ [&, index] {
                worker_tls_probe.value = static_cast<std::uint32_t>(index + 1U);
                addresses[index] = &worker_tls_probe;
                try {
                    throw static_cast<int>(worker_tls_probe.value);
                } catch (int caught) {
                    worker_tls_probe.value = static_cast<std::uint32_t>(caught);
                }
            } };
        }
        for (auto& worker : workers) {
            worker.join();
        }
        if (addresses[0] == nullptr || addresses[1] == nullptr || addresses[0] == addresses[1] ||
            addresses[0] == &worker_tls_probe || addresses[1] == &worker_tls_probe ||
            worker_tls_destructor_count.load(std::memory_order_relaxed) - destructors_before != workers.size()) {
            return false;
        }

        ExitOrderState state;
        std::thread exiting_thread{ [&] {
            std::unique_lock lock{ state.mutex };
            exit_order_tls_probe.state = &state;
            state.notification_registered.store(true, std::memory_order_release);
            std::notify_all_at_thread_exit(state.condition, std::move(lock));
        } };
        while (!state.notification_registered.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(1ms);
        }
        {
            const std::lock_guard lock{ state.mutex };
            if (!state.destructor_finished.load(std::memory_order_acquire)) {
                exiting_thread.join();
                return false;
            }
        }
        exiting_thread.join();
        return true;
    }

    [[nodiscard]] bool test_libc_reentrancy_and_entropy()
    {
        std::array<bool, 3U> errno_isolated{};
        std::barrier checkpoint{ static_cast<std::ptrdiff_t>(errno_isolated.size() + 1U) };
        std::array<std::thread, errno_isolated.size()> workers;
        for (std::size_t index{}; index < workers.size(); ++index) {
            workers[index] = std::thread{ [&, index] {
                const int expected_errno{ static_cast<int>(EAGAIN + index) };
                errno = expected_errno;
                checkpoint.arrive_and_wait();
                errno_isolated[index] = errno == expected_errno;
            } };
        }
        checkpoint.arrive_and_wait();
        for (auto& worker : workers) {
            worker.join();
        }
        if (!std::ranges::all_of(errno_isolated, std::identity{})) {
            return false;
        }

        std::array<char, 32U> formatted{};
        if (std::snprintf(formatted.data(), formatted.size(), "%s:%d", "newlib", 42) <= 0 ||
            std::string_view{ formatted.data() } != "newlib:42") {
            return false;
        }

        errno = 0;
        if (_write(STDOUT_FILENO, nullptr, 0) != 0 || _write(STDOUT_FILENO, nullptr, 1) != -1 ||
            errno != EINVAL) {
            return false;
        }

        std::array<std::byte, 256U> entropy{};
        if (_getentropy(nullptr, 0U) != 0 || _getentropy(entropy.data(), 16U) != 0) {
            return false;
        }
        errno = 0;
        if (_getentropy(nullptr, 1U) != -1 || errno != EFAULT) {
            return false;
        }
        errno = 0;
        if (_getentropy(entropy.data(), entropy.size() + 1U) != -1 || errno != EIO) {
            return false;
        }

        std::random_device random;
        static_cast<void>(random());
        return random.entropy() == std::numeric_limits<std::random_device::result_type>::digits;
    }

    [[nodiscard]] bool test_many_streams_and_threads()
    {
        namespace fs = std::filesystem;
        const fs::path directory{ "/stream-stress" };
        std::error_code error;
        static_cast<void>(fs::remove_all(directory, error));
        error.clear();
        if (!fs::create_directory(directory, error) || error) {
            return false;
        }

        std::array<std::fstream, 16U> streams;
        for (std::size_t index{}; index < streams.size(); ++index) {
            streams[index].open(directory / std::format("{}.txt", index),
                                std::ios::in | std::ios::out | std::ios::trunc);
            streams[index] << index;
            if (!streams[index]) {
                return false;
            }
        }
        for (auto& stream : streams) {
            stream.close();
            if (stream.fail()) {
                return false;
            }
        }
        static_cast<void>(fs::remove_all(directory, error));
        if (error) {
            return false;
        }

        for (std::size_t iteration{}; iteration < 128U; ++iteration) {
            std::thread thread{ [iteration] {
                const auto allocation{ std::make_unique<std::uint32_t>(static_cast<std::uint32_t>(iteration)) };
                if (*allocation != iteration) {
                    std::terminate();
                }
            } };
            thread.join();
        }
        return true;
    }

    [[nodiscard]] bool test_clocks()
    {
        const auto steady_before{ std::chrono::steady_clock::now() };
        const auto system_before{ std::chrono::system_clock::now() };
        const std::time_t c_time{ std::time(nullptr) };
        std::this_thread::sleep_for(5ms);
        const auto steady_after{ std::chrono::steady_clock::now() };
        const auto system_after{ std::chrono::system_clock::now() };
        const std::time_t chrono_time{ std::chrono::system_clock::to_time_t(system_after) };
        const auto difference{ c_time > chrono_time ? c_time - chrono_time : chrono_time - c_time };
        return std::chrono::steady_clock::is_steady && steady_after - steady_before >= 5ms &&
               system_after > system_before && c_time >= 946'684'800 && difference <= 2;
    }

    [[nodiscard]] bool test_filex_standard_library()
    {
        namespace fs = std::filesystem;
        const fs::path root{ "/runtime-self-test" };
        const fs::path nested{ root / "nested" };
        const fs::path source{ nested / "source.txt" };
        const fs::path copy{ nested / "copy.txt" };
        const fs::path target{ nested / "target.txt" };
        constexpr std::string_view CONTENT{ "FileX std::filesystem conformance" };
        std::error_code error;

        static_cast<void>(fs::remove_all(root, error));
        error.clear();
        if (!fs::create_directories(nested, error) || error) {
            return false;
        }
        {
            std::fstream stream{ source, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc };
            stream.write(CONTENT.data(), static_cast<std::streamsize>(CONTENT.size()));
            stream.seekg(0);
            std::string input(CONTENT.size(), '\0');
            stream.read(input.data(), static_cast<std::streamsize>(input.size()));
            if (!stream || input != CONTENT) {
                return false;
            }
        }
        if (!fs::copy_file(source, copy, error) || error) {
            return false;
        }
        {
            std::ofstream replacement{ target };
            replacement << "old";
        }
        fs::rename(copy, target, error);
        if (error || fs::file_size(target, error) != CONTENT.size() || error) {
            return false;
        }

        std::size_t entries{};
        for ([[maybe_unused]] const auto& entry : fs::recursive_directory_iterator{ root, error }) {
            ++entries;
        }
        if (error || entries != 3U) {
            return false;
        }

        const fs::space_info volume{ fs::space(root, error) };
        if (error || volume.capacity != 32U * 1024U || volume.available > volume.capacity) {
            return false;
        }

        fs::current_path(nested, error);
        if (error) {
            return false;
        }
        {
            std::ifstream relative{ "source.txt", std::ios::binary };
            std::string input(CONTENT.size(), '\0');
            relative.read(input.data(), static_cast<std::streamsize>(input.size()));
            if (!relative || input != CONTENT) {
                fs::current_path("/", error);
                return false;
            }
        }
        fs::current_path("/", error);
        if (error) {
            return false;
        }

        const std::uintmax_t removed{ fs::remove_all(root, error) };
        return !error && removed == 4U && !fs::exists(root, error) && !error;
    }

    struct NamedTest
    {
        const char* name;
        bool (*function)();
    };

    constexpr std::array TESTS{
        NamedTest{ "library surface", test_library_surface },
        NamedTest{ "threads and synchronization", test_threads_and_synchronization },
        NamedTest{ "atomics, futures, and exceptions", test_atomics_futures_and_exceptions },
        NamedTest{ "TLS and thread-exit order", test_tls_and_thread_exit_order },
        NamedTest{ "libc reentrancy and entropy", test_libc_reentrancy_and_entropy },
        NamedTest{ "stream and thread stress", test_many_streams_and_threads },
        NamedTest{ "clocks", test_clocks },
        NamedTest{ "FileX standard library", test_filex_standard_library },
    };

    [[noreturn]] void finish(bool passed, std::size_t phase)
    {
        runtime_hardware_self_test_status = passed ? PASS_STATUS : FAIL_STATUS | static_cast<std::uint32_t>(phase);
        log(passed ? "[runtime-self-test] PASS" : "[runtime-self-test] FAIL");
        while (true) {
            std::this_thread::sleep_for(1s);
        }
    }
}

int main()
{
    log("[runtime-self-test] ARM/Newlib/ThreadX conformance start");
    for (std::size_t index{}; index < TESTS.size(); ++index) {
        runtime_hardware_self_test_phase = static_cast<std::uint32_t>(index + 1U);
        log("[runtime-self-test] RUN  %s", TESTS[index].name);
        bool passed{};
        try {
            passed = TESTS[index].function();
        } catch (const std::exception& exception) {
            log("[runtime-self-test] exception: %s", exception.what());
        } catch (...) {
            log("[runtime-self-test] unknown exception");
        }
        if (!passed) {
            log("[runtime-self-test] FAIL %s", TESTS[index].name);
            finish(false, index + 1U);
        }
        log("[runtime-self-test] PASS %s", TESTS[index].name);
    }
    finish(true, TESTS.size());
}
