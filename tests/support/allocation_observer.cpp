// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "allocation_observer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <span>
#include <thread>
#if !DE_ALLOCATION_SANITIZER_OBSERVATION
#include <new>
#endif
#ifdef __has_feature
#if __has_feature(address_sanitizer)
#include <dlfcn.h>
#endif
#endif
#if DE_ALLOCATION_SANITIZER_OBSERVATION
#include <sanitizer/allocator_interface.h>
#endif
namespace docenhance::tests::allocation_observer {
std::atomic<bool> observing{false};
std::atomic<std::size_t> live{0};
std::atomic<std::size_t> peak{0};
constexpr std::size_t observer_worker_limit = 8;
namespace {
std::array<std::thread::id, observer_worker_limit> workers{};
std::mutex worker_mutex;
} // namespace
std::size_t worker_count = 0;
std::atomic<std::size_t> allocation_attempts{0};
std::atomic<std::size_t> failure_at{0};
std::atomic<bool> persistent_failure{false};
bool refuse_allocation() noexcept {
    if (!observing.load()) {
        return false;
    }
    const auto attempt = allocation_attempts.fetch_add(1) + 1;
    const auto failure = failure_at.load();
    return failure != 0 &&
           (attempt == failure || (persistent_failure.load() && attempt >= failure));
}
void acquire_bytes(std::size_t bytes) {
    const std::scoped_lock lock{worker_mutex};
    const auto worker = std::this_thread::get_id();
    const bool known = std::ranges::find(workers, worker) != workers.end();
    if (!known) {
        if (worker_count < workers.size()) {
            std::span{workers}.subspan(worker_count, 1).front() = worker;
            ++worker_count;
        } else {
            worker_count = observer_worker_limit + 1;
        }
    }
    const auto current = live.fetch_add(bytes) + bytes;
    auto maximum = peak.load();
    while (current > maximum && !peak.compare_exchange_weak(maximum, current)) {
    }
}
namespace {
struct ObservedAllocation {
    void* pointer = nullptr;
    std::size_t bytes = 0;
    bool measured = false;
};
constexpr std::size_t allocation_slots = 2048;
std::array<ObservedAllocation, allocation_slots> allocations{};
std::mutex allocation_mutex;
bool register_allocation(void* const pointer, std::size_t bytes) {
    bool registered = false;
    const bool measured = observing.load();
    {
        const std::scoped_lock lock{allocation_mutex};
        for (auto& slot : allocations) {
            if (slot.pointer == nullptr) {
                slot = {.pointer = pointer, .bytes = bytes, .measured = measured};
                registered = true;
                break;
            }
        }
    }
    if (registered && measured) {
        acquire_bytes(bytes);
    }
    return registered;
}
ObservedAllocation forget_allocation(const void* const pointer) noexcept {
    ObservedAllocation allocation;
    {
        const std::scoped_lock lock{allocation_mutex};
        for (auto& slot : allocations) {
            if (slot.pointer == pointer) {
                allocation = slot;
                slot = {};
                break;
            }
        }
    }
    if (allocation.measured) {
        live.fetch_sub(allocation.bytes);
    }
    return allocation;
}
#if !DE_ALLOCATION_SANITIZER_OBSERVATION
void* allocate_bytes(std::size_t bytes) {
    if (refuse_allocation()) {
        throw std::bad_alloc{};
    }
    // The replaceable ABI returns raw storage, released by the paired deallocation callback.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    auto* const pointer = std::malloc(bytes == 0 ? 1 : bytes);
    if (pointer == nullptr) {
        throw std::bad_alloc{};
    }
    const bool registered = register_allocation(pointer, bytes);
    if (!registered) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
        std::free(pointer);
        throw std::bad_alloc{};
    }
    return pointer;
}
void release_external(void* const pointer) noexcept {
#ifdef __has_feature
#if __has_feature(address_sanitizer)
    // Mach-O's sanitizer and standard-library interposition can allocate without this observer.
    // POSIX dlsym provides the next runtime's ordinary delete entry, preserving its allocation tag.
    using DeleteFunction = void (*)(void*);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    static const auto release = reinterpret_cast<DeleteFunction>(dlsym(RTLD_NEXT, "_ZdlPv"));
    if (release == nullptr) {
        std::abort();
    }
    release(pointer);
    return;
#endif
#endif
    // Unobserved ordinary CRT allocations retain the CRT's original malloc/free storage layout.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    std::free(pointer);
}
void release_bytes(void* const pointer) noexcept {
    if (pointer == nullptr) {
        return;
    }
    const auto allocation = forget_allocation(pointer);
    if (allocation.pointer == nullptr) {
        release_external(pointer);
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    std::free(pointer);
}
#endif
} // namespace
#if DE_ALLOCATION_SANITIZER_OBSERVATION
namespace {
// Compiler TLS can allocate on first access on Darwin, recursively entering the malloc hook.
// Track active callback threads in fixed storage instead; never drop another thread's event.
std::array<std::thread::id, observer_worker_limit> active_hooks{};
std::atomic_flag hooks_lock{};
void lock_hooks() noexcept {
    while (hooks_lock.test_and_set(std::memory_order_acquire)) {
    }
}
bool enter_hook() noexcept {
    const auto thread = std::this_thread::get_id();
    lock_hooks();
    if (std::ranges::find(active_hooks, thread) != active_hooks.end()) {
        hooks_lock.clear(std::memory_order_release);
        return false;
    }
    auto* const slot = std::ranges::find(active_hooks, std::thread::id{});
    if (slot == active_hooks.end()) {
        std::abort();
    }
    *slot = thread;
    hooks_lock.clear(std::memory_order_release);
    return true;
}
void leave_hook() noexcept {
    const auto thread = std::this_thread::get_id();
    lock_hooks();
    auto* const slot = std::ranges::find(active_hooks, thread);
    if (slot == active_hooks.end()) {
        std::abort();
    }
    *slot = {};
    hooks_lock.clear(std::memory_order_release);
}
void observe_malloc(const volatile void* const pointer, std::size_t bytes) noexcept {
    if (!observing.load() || !enter_hook()) {
        return;
    }
    try {
        // The sanitizer's ABI is const even though allocation storage is owned/mutable.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        const bool recorded = register_allocation(const_cast<void*>(pointer), bytes);
        if (!recorded) {
            std::abort();
        }
    } catch (...) {
        // Observation failure cannot cross a native callback or manufacture passing evidence.
        std::abort();
    }
    leave_hook();
}
void observe_free(const volatile void* const pointer) noexcept {
    if ((!observing.load() && live.load() == 0) || !enter_hook()) {
        return;
    }
    try {
        // The sanitizer exposes the pointer with volatile observation qualification.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        static_cast<void>(forget_allocation(const_cast<const void*>(pointer)));
    } catch (...) {
        std::abort();
    }
    leave_hook();
}
} // namespace
#endif
bool initialize_hooks() noexcept {
#if DE_ALLOCATION_SANITIZER_OBSERVATION
    return __sanitizer_install_malloc_and_free_hooks(observe_malloc, observe_free) != 0;
#else
    return true;
#endif
}
} // namespace docenhance::tests::allocation_observer
#if !DE_ALLOCATION_SANITIZER_OBSERVATION
// Compiler-specific standard-library declarations choose different parameter names.
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void* operator new(std::size_t bytes) {
    return docenhance::tests::allocation_observer::allocate_bytes(bytes);
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void* operator new[](std::size_t bytes) {
    return docenhance::tests::allocation_observer::allocate_bytes(bytes);
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void operator delete(void* const pointer) noexcept {
    docenhance::tests::allocation_observer::release_bytes(pointer);
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void operator delete[](void* const pointer) noexcept {
    docenhance::tests::allocation_observer::release_bytes(pointer);
}
// Pair nothrow forms too: sanitizer runtimes may otherwise bypass ordinary operator new.
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void* operator new(std::size_t bytes, const std::nothrow_t& /*tag*/) noexcept {
    try {
        return docenhance::tests::allocation_observer::allocate_bytes(bytes);
    } catch (...) {
        return nullptr;
    }
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void* operator new[](std::size_t bytes, const std::nothrow_t& /*tag*/) noexcept {
    try {
        return docenhance::tests::allocation_observer::allocate_bytes(bytes);
    } catch (...) {
        return nullptr;
    }
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void operator delete(void* const pointer, const std::nothrow_t& /*tag*/) noexcept {
    docenhance::tests::allocation_observer::release_bytes(pointer);
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void operator delete[](void* const pointer, const std::nothrow_t& /*tag*/) noexcept {
    docenhance::tests::allocation_observer::release_bytes(pointer);
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void operator delete(void* const pointer, std::size_t /*size*/) noexcept {
    docenhance::tests::allocation_observer::release_bytes(pointer);
}
// NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
void operator delete[](void* const pointer, std::size_t /*size*/) noexcept {
    docenhance::tests::allocation_observer::release_bytes(pointer);
}
#endif
