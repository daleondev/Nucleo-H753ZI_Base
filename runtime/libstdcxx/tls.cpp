#if defined(HAL_PLATFORM_STM32)

#include "hal/hal.hpp"

#include <tx_api.h>

#include <cxxabi.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

extern "C" {
extern std::byte __tdata_source[];
extern std::byte __tls_base[];
extern std::byte __tdata_source_size;
extern std::byte __tls_size;
extern std::byte __tls_align;
extern std::byte __arm32_tls_tcb_offset;
}

namespace
{
    struct ThreadLocalDestructor
    {
        void (*function)(void*);
        void* object;
        ThreadLocalDestructor* next;
    };

    ThreadLocalDestructor* startup_destructors{};

    [[nodiscard]] auto linker_value(const std::byte& symbol) noexcept -> std::size_t
    {
        return reinterpret_cast<std::uintptr_t>(&symbol);
    }

    [[noreturn]] auto tls_failure() noexcept -> void
    {
        Error_Handler();
        std::abort();
    }

    [[nodiscard]] auto tls_data(std::byte* thread_pointer_value) noexcept -> std::byte*
    {
        return thread_pointer_value + linker_value(__arm32_tls_tcb_offset);
    }

    auto initialize_tls_data(std::byte* destination, const std::byte* source) noexcept -> void
    {
        const std::size_t initialized_size{ linker_value(__tdata_source_size) };
        const std::size_t total_size{ linker_value(__tls_size) };
        if (initialized_size > total_size) {
            tls_failure();
        }

        std::memcpy(destination, source, initialized_size);
        std::memset(destination + initialized_size, 0, total_size - initialized_size);
    }

    auto discard_destructors(ThreadLocalDestructor*& first) noexcept -> void
    {
        while (first != nullptr) {
            ThreadLocalDestructor* const current{ first };
            first = current->next;
            delete current;
        }
    }

    alignas(void*) thread_local std::byte exception_globals_storage[3U * sizeof(void*)]{};
    static_assert(sizeof(exception_globals_storage) == 12U);
}

extern "C" [[gnu::noinline]] auto runtime_tls_read_thread_pointer() noexcept -> void*
{
    if (TX_THREAD* const current{ tx_thread_identify() }; current != TX_NULL) {
        if (current->tx_thread_runtime_tls_block == nullptr) {
            tls_failure();
        }
        return current->tx_thread_runtime_tls_block;
    }

    const auto startup_data{ reinterpret_cast<std::uintptr_t>(__tls_base) };
    return reinterpret_cast<void*>(startup_data - linker_value(__arm32_tls_tcb_offset));
}

// __aeabi_read_tp uses the special Arm EABI thread-pointer helper convention:
// callers may keep a TLS relocation offset live in r1-r3 across the call. A
// normal C/C++ implementation is allowed to clobber those registers, so keep
// the policy in a regular helper and provide the ABI entry point as a naked
// wrapper that preserves r1-r3 and lr. r0 carries the returned thread pointer.
extern "C" [[gnu::naked]] auto __aeabi_read_tp() noexcept -> void*
{
    __asm volatile("push {r1, r2, r3, lr}\n"
                   "bl runtime_tls_read_thread_pointer\n"
                   "pop {r1, r2, r3, pc}\n");
}

extern "C" auto runtime_tls_thread_create(TX_THREAD* thread) noexcept -> void
{
    if (thread == TX_NULL) {
        tls_failure();
    }

    const std::size_t alignment{ linker_value(__tls_align) };
    const std::size_t tcb_offset{ linker_value(__arm32_tls_tcb_offset) };
    const std::size_t tls_size{ linker_value(__tls_size) };
    if (alignment == 0U || (alignment & (alignment - 1U)) != 0U || tcb_offset < alignment ||
        tls_size > std::numeric_limits<std::size_t>::max() - tcb_offset - (alignment - 1U)) {
        tls_failure();
    }

    const std::size_t allocation_size{ tcb_offset + tls_size + alignment - 1U };
    void* const allocation{ std::malloc(allocation_size) };
    if (allocation == nullptr) {
        tls_failure();
    }

    const auto allocation_address{ reinterpret_cast<std::uintptr_t>(allocation) };
    const auto aligned_address{ (allocation_address + alignment - 1U) & ~(alignment - 1U) };
    auto* const pointer{ reinterpret_cast<std::byte*>(aligned_address) };

    thread->tx_thread_runtime_tls_allocation = allocation;
    thread->tx_thread_runtime_tls_block = pointer;
    thread->tx_thread_runtime_tls_destructors = nullptr;
    initialize_tls_data(tls_data(pointer), __tdata_source);
}

extern "C" auto runtime_tls_adopt_startup(TX_THREAD* thread) noexcept -> void
{
    if (thread == TX_NULL || thread->tx_thread_runtime_tls_block == nullptr) {
        tls_failure();
    }

    // Global constructors execute in the initial execution context and may
    // already have initialized non-trivial thread_local objects, published
    // their addresses, and registered destructors that point into the linker
    // TLS image. Relocating those live objects with memcpy would violate their
    // identity and leave the destructor registrations pointing at stale data.
    // Make the application ThreadX thread continue using the original image.
    std::free(thread->tx_thread_runtime_tls_allocation);
    const auto startup_data{ reinterpret_cast<std::uintptr_t>(__tls_base) };
    thread->tx_thread_runtime_tls_allocation = nullptr;
    thread->tx_thread_runtime_tls_block =
      reinterpret_cast<void*>(startup_data - linker_value(__arm32_tls_tcb_offset));
    thread->tx_thread_runtime_tls_destructors = startup_destructors;
    startup_destructors = nullptr;
}

extern "C" auto runtime_tls_thread_exit(TX_THREAD* thread) noexcept -> void
{
    if (thread == TX_NULL) {
        tls_failure();
    }

    auto*& destructor_pointer{ thread->tx_thread_runtime_tls_destructors };
    auto* current{ static_cast<ThreadLocalDestructor*>(destructor_pointer) };
    while (current != nullptr) {
        destructor_pointer = current->next;
        current->function(current->object);
        delete current;
        current = static_cast<ThreadLocalDestructor*>(destructor_pointer);
    }
}

extern "C" auto runtime_tls_thread_delete(TX_THREAD* thread) noexcept -> void
{
    if (thread == TX_NULL) {
        tls_failure();
    }

    auto* destructors{ static_cast<ThreadLocalDestructor*>(thread->tx_thread_runtime_tls_destructors) };
    discard_destructors(destructors);
    thread->tx_thread_runtime_tls_destructors = nullptr;
    std::free(thread->tx_thread_runtime_tls_allocation);
    thread->tx_thread_runtime_tls_allocation = nullptr;
    thread->tx_thread_runtime_tls_block = nullptr;
}

namespace __cxxabiv1
{
    extern "C" auto __cxa_thread_atexit(void (*destructor)(void*), void* object, void* dso_handle) noexcept
      -> int
    {
        static_cast<void>(dso_handle);
        auto* const element{ new (std::nothrow) ThreadLocalDestructor{
          .function = destructor, .object = object, .next = nullptr } };
        if (element == nullptr || destructor == nullptr) {
            delete element;
            return -1;
        }

        if (TX_THREAD* const current{ tx_thread_identify() }; current != TX_NULL) {
            element->next = static_cast<ThreadLocalDestructor*>(current->tx_thread_runtime_tls_destructors);
            current->tx_thread_runtime_tls_destructors = element;
        }
        else {
            element->next = startup_destructors;
            startup_destructors = element;
        }
        return 0;
    }

    extern "C" auto __cxa_get_globals() noexcept -> __cxa_eh_globals*
    {
        return reinterpret_cast<__cxa_eh_globals*>(exception_globals_storage);
    }

    extern "C" auto __cxa_get_globals_fast() noexcept -> __cxa_eh_globals*
    {
        return reinterpret_cast<__cxa_eh_globals*>(exception_globals_storage);
    }
}

#endif
