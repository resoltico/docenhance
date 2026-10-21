// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "allocation_observer.hpp"

#include <cstddef>
#include <cstdlib>
#include <utility>
#ifdef __APPLE__
namespace {
// dyld reads two pointer-width addresses per record. Each pair retains its actual C ABI type;
// no object/function pointer conversion or substitute allocator is involved.
struct AllocateInterpose {
    decltype(&std::malloc) replacement;
    decltype(&std::malloc) original;
};
struct ReleaseInterpose {
    decltype(&std::free) replacement;
    decltype(&std::free) original;
};
static_assert(sizeof(AllocateInterpose) == 2 * sizeof(void*));
static_assert(sizeof(ReleaseInterpose) == 2 * sizeof(void*));
[[gnu::used, gnu::section("__DATA,__interpose")]] const AllocateInterpose allocate_entry{
    .replacement = docenhance::tests::allocation_observer::c_allocate,
    .original = std::malloc,
};
[[gnu::used, gnu::section("__DATA,__interpose")]] const ReleaseInterpose release_entry{
    .replacement = docenhance::tests::allocation_observer::c_release,
    .original = std::free,
};
} // namespace
#endif

#if defined(__linux__) && defined(DE_ALLOCATION_C_OBSERVATION)

// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern "C" void* __wrap_malloc(std::size_t bytes) noexcept;
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern "C" void __wrap_free(void* pointer) noexcept;
extern "C" void* __wrap_malloc(std::size_t bytes) noexcept {
    return docenhance::tests::allocation_observer::c_allocate(bytes);
}
extern "C" void __wrap_free(void* const pointer) noexcept {
    docenhance::tests::allocation_observer::c_release(pointer);
}
#endif

#ifdef DE_ALLOCATION_C_OBSERVATION

#include <array>
#include <atomic>
#include <cerrno>
#include <iostream>
#include <mutex>

#ifdef _WIN32
#include <corecrt_malloc.h>
#include <crtdbg.h>
#include <new.h>
#include <type_traits>
#endif
#ifdef __linux__
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern "C" void* __real_malloc(std::size_t bytes) noexcept;
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern "C" void __real_free(void* pointer) noexcept;
#endif
namespace docenhance::tests::allocation_observer {
void* original_allocate(std::size_t bytes) noexcept(
    noexcept(std::declval<decltype(&std::malloc)>()(bytes))) {
#if defined(_WIN32) && defined(_DEBUG)
    return _malloc_dbg(bytes, _NORMAL_BLOCK, nullptr, 0);
#elifdef _WIN32
    return _malloc_base(bytes);
#elifdef __linux__
    return __real_malloc(bytes);
#else
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,clang-analyzer-optin.portability.UnixAPI)
    return std::malloc(bytes);
#endif
}
void original_release(void* const pointer) noexcept(
    noexcept(std::declval<decltype(&std::free)>()(pointer))) {
#if defined(_WIN32) && defined(_DEBUG)
    _free_dbg(pointer, _NORMAL_BLOCK);
#elifdef _WIN32
    _free_base(pointer);
#elifdef __linux__
    __real_free(pointer);
#else
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)
    std::free(pointer);
#endif
}
namespace {
#ifdef _WIN32
#ifdef _DLL
#error "The resource observer requires the configuration-specific static CRT"
#endif
static_assert(std::is_same_v<decltype(&std::malloc), decltype(&c_allocate)>);
static_assert(std::is_same_v<decltype(&std::free), decltype(&c_release)>);
#ifdef _DEBUG
static_assert(noexcept(c_allocate(0)) == noexcept(_malloc_dbg(0, _NORMAL_BLOCK, nullptr, 0)));
static_assert(noexcept(c_release(nullptr)) == noexcept(_free_dbg(nullptr, _NORMAL_BLOCK)));
#else
static_assert(std::is_same_v<decltype(&original_allocate), decltype(&_malloc_base)>);
static_assert(std::is_same_v<decltype(&original_release), decltype(&_free_base)>);
#endif
std::atomic<unsigned> native_handler_visits{0};
int reject_native_new(std::size_t) noexcept {
    native_handler_visits.fetch_add(1);
    return 0;
}
struct NativeAllocationPolicy {
    NativeAllocationPolicy()
        : mode(_set_new_mode(1)), handler(_set_new_handler(reject_native_new)) {}
    ~NativeAllocationPolicy() {
        static_cast<void>(_set_new_handler(handler));
        static_cast<void>(_set_new_mode(mode));
    }
    NativeAllocationPolicy(const NativeAllocationPolicy&) = delete;
    NativeAllocationPolicy& operator=(const NativeAllocationPolicy&) = delete;
    int mode;
    decltype(_set_new_handler(nullptr)) handler;
};
#endif
void begin_c_case(std::size_t fail = 0) {
    allocation_attempts.store(0);
    c_allocation_attempts.store(0);
    peak.store(0);
    failure_at.store(fail);
    observing.store(true);
}
bool zero_c_case() {
    errno = EDOM;
    auto* const native = original_allocate(0);
    const int native_errno = errno;
    const bool native_null = native == nullptr;
    original_release(native);
    begin_c_case();
    errno = EDOM;
    auto* const pointer = c_allocate(0);
    const int observed_errno = errno;
    const bool observed_null = pointer == nullptr;
#if defined(_WIN32) && defined(_DEBUG)
    bool exact_zero = true;
    if (pointer != nullptr) {
        exact_zero = _msize_dbg(pointer, _NORMAL_BLOCK) == 0;
    }
#else
    constexpr bool exact_zero = true;
#endif
    c_release(pointer);
    observing.store(false);
    return exact_zero && observed_null == native_null && observed_errno == native_errno &&
           allocation_attempts.load() == 1 && c_allocation_attempts.load() == 1 &&
           live.load() == 0 && peak.load() == 0;
}
bool refused_c_case(std::size_t bytes) {
    begin_c_case(1);
    errno = EDOM;
    auto* const pointer = c_allocate(bytes);
    const int refusal_errno = errno;
    const bool refused = pointer == nullptr;
    // A failed predicate still releases any unexpected real returned allocation.
    c_release(pointer);
    observing.store(false);
    failure_at.store(0);
    return refused && refusal_errno == ENOMEM && allocation_attempts.load() == 1 &&
           c_allocation_attempts.load() == 1 && live.load() == 0 && peak.load() == 0;
}
bool cpp_zero_case() {
    begin_c_case();
    auto* const pointer = ::operator new(0);
    const bool allocated = pointer != nullptr;
#if defined(_WIN32) && defined(_DEBUG)
    bool normalized = true;
    normalized = _msize_dbg(pointer, _NORMAL_BLOCK) == 1;
#else
    constexpr bool normalized = true;
#endif
    ::operator delete(pointer);
    observing.store(false);
    return allocated && normalized && allocation_attempts.load() == 1 &&
           c_allocation_attempts.load() == 0 && live.load() == 0 && peak.load() == 0;
}
bool uncertain_release_case(bool recorded, std::size_t bytes, bool null_pointer) {
    std::byte identity{};
    std::array<detail::ObservedAllocation, 1> slots{};
    if (recorded) {
        slots.front() = {
            .pointer = &identity,
            .bytes = bytes,
            .measured = false,
            .retiring = false,
        };
    }
    std::mutex mutex;
    std::atomic<bool> valid{true};
    const auto* const pointer = null_pointer ? nullptr : &identity;
    const int entering_errno = errno;
    {
        const detail::CReleaseReservation uncertain{slots, mutex, pointer, valid};
        errno = ERANGE;
    }
    const bool preserved_errno = errno == ERANGE;
    bool ineligible = false;
    {
        const std::scoped_lock held{mutex};
        ineligible = detail::find_allocation(slots, pointer) == nullptr;
    }
    const bool retained = !recorded || (slots.front().pointer == &identity &&
                                        slots.front().bytes == bytes && slots.front().retiring);
    // A later confirmed null release cannot reactivate a failed epoch.
    {
        detail::CReleaseReservation later{slots, mutex, nullptr, valid};
        later.complete();
    }
    const bool quarantined = !valid.load();
    errno = entering_errno;
    return preserved_errno && retained && ineligible && quarantined;
}
bool confirmed_empty_release_case(bool null_pointer) {
    std::byte identity{};
    std::array<detail::ObservedAllocation, 1> slots{};
    std::mutex mutex;
    std::atomic<bool> valid{true};
    {
        detail::CReleaseReservation confirmed{slots, mutex, null_pointer ? nullptr : &identity,
                                              valid};
        confirmed.complete();
    }
    return valid.load() && slots.front().pointer == nullptr;
}
bool release_reservation_cases() {
    // Bookkeeping-only local metadata/health; never a fake provider or native effect claim.
    std::byte identity{};
    std::array<detail::ObservedAllocation, 2> slots{};
    slots.front() = {.pointer = &identity, .bytes = 137, .measured = false, .retiring = false};
    std::mutex mutex;
    std::atomic<bool> valid{true};
    {
        detail::CReleaseReservation first{slots, mutex, &identity, valid};
        slots.back() = {.pointer = &identity, .bytes = 211, .measured = false, .retiring = false};
        {
            const std::scoped_lock held{mutex};
            if (detail::find_allocation(slots, &identity) != &slots.back()) {
                return false;
            }
        }
        detail::CReleaseReservation reused{slots, mutex, &identity, valid};
        if (!slots.front().retiring || !slots.back().retiring) {
            return false;
        }
        reused.complete();
        if (slots.front().pointer != &identity || slots.front().bytes != 137 ||
            !slots.front().retiring || slots.back().pointer != nullptr) {
            return false;
        }
        first.complete();
    }
    if (!valid.load() || slots.front().pointer != nullptr || slots.back().pointer != nullptr) {
        return false;
    }
    return uncertain_release_case(true, 137, false) && uncertain_release_case(true, 0, false) &&
           uncertain_release_case(false, 0, false) && uncertain_release_case(false, 0, true) &&
           confirmed_empty_release_case(false) && confirmed_empty_release_case(true);
}
bool null_release_case() {
    errno = ERANGE;
    original_release(nullptr);
    const int native_errno = errno;
    begin_c_case();
    errno = ERANGE;
    c_release(nullptr);
    const int observed_errno = errno;
    observing.store(false);
    return observed_errno == native_errno && allocation_attempts.load() == 0 &&
           c_allocation_attempts.load() == 0 && live.load() == 0 && peak.load() == 0;
}
} // namespace
bool c_contract_cases() {
    const bool reservation_valid = release_reservation_cases();
    bool passed = reservation_valid;
#ifdef _WIN32
    native_handler_visits.store(0);
    int previous_mode = 0;
    decltype(_set_new_handler(nullptr)) previous_handler = nullptr;
    {
        const NativeAllocationPolicy policy;
        previous_mode = policy.mode;
        previous_handler = policy.handler;
        passed = passed && _query_new_mode() == 1 && zero_c_case() && null_release_case() &&
                 cpp_zero_case() && refused_c_case(0) && refused_c_case(137) &&
                 native_handler_visits.load() == 0;
    }
    const bool handler_restored = _set_new_handler(previous_handler) == previous_handler;
    const bool mode_restored = _set_new_mode(previous_mode) == previous_mode;
    passed = passed && handler_restored && mode_restored;
#else
    passed = passed && zero_c_case() && null_release_case() && cpp_zero_case() &&
             refused_c_case(0) && refused_c_case(137);
#endif
    std::cout << R"({"release_reservation_protocol":)" << reservation_valid
              << R"(,"native_c_zero_errno_and_injected_refusal":)" << passed
              << R"(,"provider_exception_attainment":"unavailable"}
)";
    return passed;
}
} // namespace docenhance::tests::allocation_observer
#endif

#if defined(_WIN32) && defined(DE_ALLOCATION_C_OBSERVATION)
extern "C" void* __cdecl
malloc(std::size_t bytes) noexcept(noexcept(std::declval<decltype(&std::malloc)>()(bytes))) {
    return docenhance::tests::allocation_observer::c_allocate(bytes);
}
extern "C" void __cdecl
free(void* pointer) noexcept(noexcept(std::declval<decltype(&std::free)>()(pointer))) {
    docenhance::tests::allocation_observer::c_release(pointer);
}
#endif
