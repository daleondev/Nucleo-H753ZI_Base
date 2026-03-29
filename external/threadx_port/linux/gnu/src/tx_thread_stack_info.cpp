#include "tx_thread_stack_info.hpp"

#include "pthread.h"
#include "tx_thread.h"

#include <cassert>
#include <csignal>
#include <cstdlib>

#include <memory>
#include <optional>
#include <ranges>
#include <system_error>

namespace Tx::Linux
{
    namespace
    {
        using StatusType = decltype(TX_SUCCESS);
        enum class Status : StatusType
        {
            SUCCESS = TX_SUCCESS,
            DELETED,
            POOL_ERROR,
            PTR_ERROR,
            WAIT_ERROR,
            SIZE_ERROR,
            GROUP_ERROR,
            NO_EVENTS,
            OPTION_ERROR,
            QUEUE_ERROR,
            QUEUE_EMPTY,
            QUEUE_FULL,
            SEMAPHORE_ERROR,
            NO_INSTANCE,
            THREAD_ERROR,
            PRIORITY_ERROR,
            NO_MEMORY,
            START_ERROR,
            DELETE_ERROR,
            RESUME_ERROR,
            CALLER_ERROR,
            SUSPEND_ERROR,
            TIMER_ERROR,
            TICK_ERROR,
            ACTIVATE_ERROR,
            THRESH_ERROR,
            SUSPEND_LIFTED,
            WAIT_ABORTED,
            WAIT_ABORT_ERROR,
            MUTEX_ERROR,
            NOT_AVAILABLE,
            NOT_OWNED,
            INHERIT_ERROR,
            NOT_DONE,
            CEILING_EXCEEDED,
            INVALID_CEILING,
            FEATURE_NOT_ENABLED = TX_FEATURE_NOT_ENABLED
        };
        constexpr auto getStatus(StatusType status) -> Status { return static_cast<Status>(status); }

        class StatusCategory : public std::error_category
        {
          public:
            const char* name() const noexcept override { return "TxStatus"; }
            std::string message(int ev) const override { return std::to_string(static_cast<StatusType>(ev)); }
        };

        std::error_code make_error_code(Status e)
        {
            assert(e != Status::SUCCESS);
            return std::error_code(static_cast<int>(e), []() -> std::error_category& {
                static StatusCategory instance;
                return instance;
            }());
        }

        template<auto DeleteFn>
        struct CustomDeleter
        {
            template<typename T>
            void operator()(T* ptr) const noexcept
            {
                if (ptr) {
                    DeleteFn(ptr);
                }
            }
        };

        struct StackInfo
        {
            std::unique_ptr<std::byte[], CustomDeleter<free>> host_stack_base;
            size_t host_stack_size{ 0 };
            stack_t signal_stack{};
            std::byte* baseline_host_stack_ptr{ nullptr };

            StackInfo(std::unique_ptr<std::byte[], CustomDeleter<free>>&& host_stack_base)
              : host_stack_base(std::move(host_stack_base))
            {
            }

            ~StackInfo()
            {
                if (signal_stack.ss_sp) {
                    delete[] static_cast<std::byte*>(signal_stack.ss_sp);
                }
            }

            static StackInfo* of(TX_THREAD* thread)
            {
                if (!thread || !thread->tx_thread_extension_ptr) {
                    return nullptr;
                }
                return static_cast<StackInfo*>(thread->tx_thread_extension_ptr);
            }
        };

        void* stackPtr()
        {
            void* stack_ptr;
            __asm__ volatile("movq %%rsp, %0" : "=r"(stack_ptr));
            return (stack_ptr);
        }

        thread_local TX_THREAD* g_thread{ nullptr };

        std::optional<std::error_code> prepare(TX_THREAD* thread_ptr)
        {
            assert(thread_ptr);

            auto requested_size{ thread_ptr->tx_thread_stack_size };
            if (requested_size == 0U) {
                return make_error_code(Status::SIZE_ERROR);
            }

            if (auto stack_info{ StackInfo::of(thread_ptr) }; stack_info) {
                delete stack_info;
                thread_ptr->tx_thread_extension_ptr = nullptr;
            }

            auto page_size{ sysconf(_SC_PAGESIZE) };
            if (page_size <= 0) {
                page_size = 4096;
            }

            auto stackRoundUp = [](size_t size, size_t alignment) -> size_t {
                return (size + alignment - 1) & ~(alignment - 1);
            };

            auto minimum_host_size{ requested_size + PTHREAD_STACK_MIN + sizeof(ULONG) };
            auto host_stack_size{ stackRoundUp(minimum_host_size, page_size) };

            void* host_stack_base_ptr{ nullptr };
            if (posix_memalign(&host_stack_base_ptr, page_size, host_stack_size) != 0) {
                return make_error_code(Status::NO_MEMORY);
            }

            auto host_stack_base{ std::unique_ptr<std::byte[], CustomDeleter<free>>{
              static_cast<std::byte*>(host_stack_base_ptr) } };
            std::fill_n(host_stack_base.get(), host_stack_size, static_cast<std::byte>(TX_STACK_FILL));

            try {
                auto signal_stack_size{ static_cast<size_t>(SIGSTKSZ) };
                std::unique_ptr<std::byte[]> signal_stack_base{ std::make_unique<std::byte[]>(
                  signal_stack_size) };

                auto stack_info{ std::make_unique<StackInfo>(std::move(host_stack_base)) };
                stack_info->host_stack_size = host_stack_size;
                stack_info->signal_stack = stack_t{ .ss_sp = signal_stack_base.release(),
                                                    .ss_flags = 0,
                                                    .ss_size = signal_stack_size };

                thread_ptr->tx_thread_extension_ptr = stack_info.release();
            } catch (const std::bad_alloc&) {
                return make_error_code(Status::NO_MEMORY);
            }

            return std::nullopt;
        }

        std::optional<std::error_code> update(TX_THREAD* thread_ptr, void* stack_ptr = nullptr)
        {
            if (!thread_ptr || (thread_ptr->tx_thread_id != TX_THREAD_ID) || !stack_ptr ||
                !thread_ptr->tx_thread_stack_start || !thread_ptr->tx_thread_stack_end) {
                return make_error_code(Status::PTR_ERROR);
            }

            auto stack_info{ Tx::Linux::StackInfo::of(thread_ptr) };
            if (!stack_info) {
                return make_error_code(Status::PTR_ERROR);
            }

            auto stack_start{ reinterpret_cast<std::byte*>(thread_ptr->tx_thread_stack_start) };
            auto stack_end{ reinterpret_cast<std::byte*>(thread_ptr->tx_thread_stack_end) };
            auto current_stack_ptr{ reinterpret_cast<std::byte*>(stack_ptr) };
            auto host_stack_base{ reinterpret_cast<std::byte*>(stack_info->host_stack_base.get()) };
            auto logical_stack_size{ static_cast<size_t>(thread_ptr->tx_thread_stack_size) };

            if ((current_stack_ptr < host_stack_base) ||
                (current_stack_ptr >= (host_stack_base + stack_info->host_stack_size))) {
                return make_error_code(Status::PTR_ERROR);
            }

            if (!stack_info->baseline_host_stack_ptr) {
                stack_info->baseline_host_stack_ptr = current_stack_ptr;
            }

            std::byte* logical_stack_ptr{ nullptr };
            if (current_stack_ptr >= stack_info->baseline_host_stack_ptr) {
                logical_stack_ptr = stack_end;
            }
            else {
                size_t host_stack_delta{ static_cast<size_t>(stack_info->baseline_host_stack_ptr -
                                                             current_stack_ptr) };
                if (host_stack_delta >= logical_stack_size) {
                    logical_stack_ptr = stack_start;
                }
                else {
                    logical_stack_ptr = stack_end - host_stack_delta;
                }
            }

            thread_ptr->tx_thread_stack_ptr = logical_stack_ptr;

#ifdef TX_ENABLE_STACK_CHECKING
            if (!thread_ptr->tx_thread_stack_highest_ptr ||
                (logical_stack_ptr < (static_cast<std::byte*>(thread_ptr->tx_thread_stack_highest_ptr)))) {
                thread_ptr->tx_thread_stack_highest_ptr = logical_stack_ptr;
            }
#endif
            return std::nullopt;
        }
    }

    void refreshAllThreadsStackInfo()
    {
        auto* first_thread{ _tx_thread_created_ptr };
        if (!first_thread || _tx_thread_created_count == 0U) {
            return;
        }

        auto* current_thread{ first_thread };
        do {
            _tx_linux_thread_stack_refresh(current_thread);
            current_thread = current_thread->tx_thread_created_next;
        } while (current_thread != first_thread);
    }
}

UINT _tx_linux_thread_stack_prepare_host(TX_THREAD* thread_ptr)
{
    if (auto error{ Tx::Linux::prepare(thread_ptr) }; error) {
        return static_cast<UINT>(error->value());
    }
    return TX_SUCCESS;
}

VOID* _tx_linux_thread_stack_host_base(TX_THREAD* thread_ptr)
{
    if (auto stack_info{ Tx::Linux::StackInfo::of(thread_ptr) }; stack_info) {
        return stack_info->host_stack_base.get();
    }
    return nullptr;
}

size_t _tx_linux_thread_stack_host_size(TX_THREAD* thread_ptr)
{
    if (auto stack_info{ Tx::Linux::StackInfo::of(thread_ptr) }; stack_info) {
        return stack_info->host_stack_size;
    }
    return 0;
}

VOID _tx_linux_thread_stack_register(TX_THREAD* thread_ptr) { Tx::Linux::g_thread = thread_ptr; }

VOID _tx_linux_thread_stack_unregister(VOID) { Tx::Linux::g_thread = nullptr; }

VOID _tx_linux_thread_stack_enable_signal_altstack(TX_THREAD* thread_ptr)
{
    if (auto stack_info{ Tx::Linux::StackInfo::of(thread_ptr) }; stack_info) {
        sigaltstack(&stack_info->signal_stack, nullptr);
    }
}

VOID _tx_linux_thread_stack_calibrate(TX_THREAD* thread_ptr)
{
    if (auto stack_info{ Tx::Linux::StackInfo::of(thread_ptr) }; stack_info) {
        stack_info->baseline_host_stack_ptr = static_cast<std::byte*>(Tx::Linux::stackPtr());
        thread_ptr->tx_thread_stack_ptr = thread_ptr->tx_thread_stack_end;
#ifdef TX_ENABLE_STACK_CHECKING
        thread_ptr->tx_thread_stack_highest_ptr = thread_ptr->tx_thread_stack_end;
#endif
    }
}

VOID _tx_linux_thread_stack_capture_current(TX_THREAD* thread_ptr)
{
    (void)Tx::Linux::update(thread_ptr, Tx::Linux::stackPtr());
}

VOID _tx_linux_thread_stack_capture_signal_context(VOID* context)
{
    if (!Tx::Linux::g_thread || !context) {
        return;
    }

    auto ucontext{ static_cast<ucontext_t*>(context) };
    auto stack_ptr{ reinterpret_cast<void*>(ucontext->uc_mcontext.gregs[REG_RSP]) };
    (void)Tx::Linux::update(Tx::Linux::g_thread, stack_ptr);
}

VOID _tx_linux_thread_stack_refresh(TX_THREAD* thread_ptr)
{
    if (!thread_ptr || (thread_ptr->tx_thread_id != TX_THREAD_ID)) {
        return;
    }

    if (pthread_equal(thread_ptr->tx_thread_linux_thread_id, pthread_self())) {
        _tx_linux_thread_stack_capture_current(thread_ptr);
    }
}