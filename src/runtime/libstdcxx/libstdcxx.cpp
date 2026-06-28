// NOLINTNEXTLINE(bugprone-reserved-identifier,cppcoreguidelines-macro-usage,readability-identifier-naming)
#define _GLIBCXX_THREAD_IMPL 1

#include "backend.hpp"

#include <bits/functexcept.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>

#include <cerrno>
#include <cstdint>
#include <utility>

#if !defined(THREADX_STD_ENABLED)
#error "The standard library must use the ThreadX gthread port"
#endif

static_assert(std::is_same_v<__gthread_t, TX_THREAD*>);
static_assert(std::is_same_v<std::thread::native_handle_type, TX_THREAD*>);
static_assert(std::is_same_v<std::chrono::high_resolution_clock, std::chrono::system_clock>);

namespace
{
    void* run_thread_state(void* state_pointer)
    {
        std::unique_ptr<std::thread::_State> state{ static_cast<std::thread::_State*>(state_pointer) };
        state->_M_run();
        return nullptr;
    }
}

namespace std
{
    thread::_State::~_State() = default;

    void thread::_M_start_thread(_State_ptr state, void (*dependency)())
    {
        static_cast<void>(dependency);
        __gthread_t native_thread{};
        const int error{ __gthread_create(&native_thread, run_thread_state, state.get()) };
        if (error != 0) {
            std::__throw_system_error(error);
        }
        _M_id = id(native_thread);
        [[maybe_unused]] auto* const transferred_state{ state.release() };
    }

    void thread::join()
    {
        if (!joinable()) {
            std::__throw_system_error(EINVAL);
        }
        if (get_id() == this_thread::get_id()) {
            std::__throw_system_error(EDEADLK);
        }
        const int error{ __gthread_join(_M_id._M_thread, nullptr) };
        if (error != 0) {
            std::__throw_system_error(error);
        }
        _M_id = id{};
    }

    void thread::detach()
    {
        if (!joinable()) {
            std::__throw_system_error(EINVAL);
        }
        const int error{ __gthread_detach(_M_id._M_thread) };
        if (error != 0) {
            std::__throw_system_error(error);
        }
        _M_id = id{};
    }

    unsigned int thread::hardware_concurrency() noexcept { return 1U; }

    condition_variable::condition_variable() noexcept = default;
    condition_variable::~condition_variable() noexcept = default;

    void condition_variable::notify_one() noexcept { _M_cond.notify_one(); }
    void condition_variable::notify_all() noexcept { _M_cond.notify_all(); }

    void condition_variable::wait(unique_lock<mutex>& lock)
    {
        if (!lock.owns_lock() || lock.mutex() == nullptr) {
            std::__throw_system_error(EPERM);
        }
        _M_cond.wait(*lock.mutex());
    }

    namespace chrono
    {
        steady_clock::time_point steady_clock::now() noexcept
        {
            return time_point{ duration{ runtime::detail::steady_time_nanoseconds() } };
        }

        system_clock::time_point system_clock::now() noexcept
        {
            // GCC aliases high_resolution_clock to system_clock on both
            // supported toolchains, so this definition implements both clocks.
            return time_point{ duration{ runtime::detail::system_time_nanoseconds() } };
        }
    }

    namespace this_thread
    {
        // This is the libstdc++ ABI entry point declared by <thread>.
        // NOLINTNEXTLINE(bugprone-reserved-identifier)
        void __sleep_for(chrono::seconds seconds, chrono::nanoseconds nanoseconds)
        {
            if (seconds.count() < 0 || nanoseconds.count() < 0) {
                return;
            }
            constexpr std::uint64_t nanoseconds_per_second{ 1'000'000'000ULL };
            const auto seconds_count{ static_cast<std::uint64_t>(seconds.count()) };
            if (seconds_count > (std::numeric_limits<std::uint64_t>::max() / nanoseconds_per_second)) {
                runtime::detail::sleep_for(std::numeric_limits<std::uint64_t>::max());
                return;
            }
            runtime::detail::sleep_for((seconds_count * nanoseconds_per_second) +
                                       static_cast<std::uint64_t>(nanoseconds.count()));
        }
    }
}
