// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "allocation_observer.hpp"
#include "fft_allocation_trace.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#ifdef __has_feature
#if __has_feature(address_sanitizer)
#include <sanitizer/allocator_interface.h>
#endif
#endif
namespace docenhance::tests::allocation_calibration {
namespace allocation = allocation_observer;
namespace trace = fft_allocation_trace;
inline void warm_exception_runtime() {
    try {
        throw std::bad_alloc{};
    } catch (const std::bad_alloc&) {
        return;
    }
}
inline void reset_observation() noexcept {
    allocation::allocation_attempts.store(0);
    allocation::c_allocation_attempts.store(0);
    trace::reset();
    allocation::peak.store(0);
}
inline bool backing_valid(const void* const pointer) noexcept {
#ifdef __has_feature
#if __has_feature(address_sanitizer)
    return pointer != nullptr && __sanitizer_get_ownership(pointer) != 0;
#endif
#endif
    return pointer != nullptr;
}
inline void report_calibration(const char* const family, bool measured, bool allocated,
                               std::size_t acquired, bool owned) {
    std::cout << R"({"calibration_family":")" << family << R"(","measured":)" << measured
              << ",\"allocated\":" << allocated << ",\"acquired\":" << acquired
              << ",\"backing_valid\":" << owned
              << ",\"attempts\":" << allocation::allocation_attempts.load()
              << ",\"c_attempts\":" << allocation::c_allocation_attempts.load()
              << ",\"peak\":" << allocation::peak.load() << ",\"live\":" << allocation::live.load()
              << "}\n";
}
inline bool self_check(bool measured = true) {
    reset_observation();
    allocation::observing.store(measured);
    decltype(&std::malloc) const volatile allocate = &std::malloc;
    decltype(&std::free) const volatile release = &std::free;
    const auto deallocate = release;
    std::unique_ptr<void, decltype(&std::free)> pointer{allocate(137), deallocate};
    const bool allocated = pointer != nullptr;
    const bool owned = backing_valid(pointer.get());
    const auto acquired = allocation::live.load();
    pointer.reset();
    allocation::observing.store(false);
    report_calibration("c", measured, allocated, acquired, owned);
    if (!measured) {
        return allocated && owned && acquired == 0 && allocation::live.load() == 0 &&
               allocation::allocation_attempts.load() == 0 && allocation::peak.load() == 0 &&
               allocation::c_allocation_attempts.load() == 0;
    }
    return allocated && owned && acquired == 137 && allocation::live.load() == 0 &&
           allocation::allocation_attempts.load() == 1 && allocation::peak.load() == 137 &&
           allocation::c_allocation_attempts.load() == 1;
}
inline bool cpp_self_check(bool measured = true) {
    reset_observation();
    allocation::observing.store(measured);
    std::size_t acquired = 0;
    bool owned = false;
    {
        using New = void* (*)(std::size_t);
        using Delete = void (*)(void*) noexcept;
        const volatile auto allocate = static_cast<New>(&::operator new);
        const volatile auto release = static_cast<Delete>(&::operator delete);
        const auto deallocate = release;
        const std::unique_ptr<void, Delete> payload{allocate(211), deallocate};
        acquired = allocation::live.load();
        owned = backing_valid(payload.get());
    }
    allocation::observing.store(false);
    report_calibration("cpp", measured, true, acquired, owned);
    if (!measured) {
        return owned && acquired == 0 && allocation::live.load() == 0 &&
               allocation::allocation_attempts.load() == 0 && allocation::peak.load() == 0 &&
               allocation::c_allocation_attempts.load() == 0;
    }
    return owned && acquired == 211 && allocation::live.load() == 0 &&
           allocation::allocation_attempts.load() == 1 && allocation::peak.load() == 211 &&
           allocation::c_allocation_attempts.load() == 0;
}
} // namespace docenhance::tests::allocation_calibration
