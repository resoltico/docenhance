// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "allocation_calibration.hpp"
#include "allocation_observer.hpp"
#include "fft_allocation_trace.hpp"
#include "restoration_resource_fixture.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <span>

namespace docenhance::tests {
namespace restoration_allocation = allocation_observer;
namespace restoration_trace = fft_allocation_trace;
inline void restoration_warm_exception_runtime() {
    allocation_calibration::warm_exception_runtime();
}
class RestorationObservationScope {
  public:
    explicit RestorationObservationScope(std::size_t failure) {
        restoration_allocation::reset_workers();
        restoration_trace::reset();
        restoration_allocation::peak.store(0);
        restoration_allocation::allocation_attempts.store(0);
        restoration_allocation::c_allocation_attempts.store(0);
        restoration_allocation::persistent_failure.store(false);
        restoration_allocation::failure_at.store(failure);
        restoration_allocation::observing.store(true);
    }
    ~RestorationObservationScope() {
        restoration_allocation::observing.store(false);
        restoration_allocation::failure_at.store(0);
        restoration_allocation::persistent_failure.store(false);
    }
    RestorationObservationScope(const RestorationObservationScope&) = delete;
    RestorationObservationScope& operator=(const RestorationObservationScope&) = delete;
    RestorationObservationScope(RestorationObservationScope&&) = delete;
    RestorationObservationScope& operator=(RestorationObservationScope&&) = delete;
};
inline void restoration_resource_calibration() {
    restoration_warm_exception_runtime();
    require(restoration_allocation::initialize_hooks(), "actual observer initialization succeeds");
    struct CalibrationCleanup {
        ~CalibrationCleanup() {
            restoration_allocation::observing.store(false);
            restoration_allocation::failure_at.store(0);
            restoration_allocation::persistent_failure.store(false);
        }
        CalibrationCleanup() = default;
        CalibrationCleanup(const CalibrationCleanup&) = delete;
        CalibrationCleanup& operator=(const CalibrationCleanup&) = delete;
        CalibrationCleanup(CalibrationCleanup&&) = delete;
        CalibrationCleanup& operator=(CalibrationCleanup&&) = delete;
    };
    const CalibrationCleanup cleanup;
    restoration_allocation::observing.store(false);
    restoration_allocation::reset_workers();
    restoration_allocation::failure_at.store(0);
    restoration_allocation::persistent_failure.store(false);
    const bool passed =
        allocation_calibration::self_check() && allocation_calibration::cpp_self_check() &&
        allocation_calibration::self_check(false) &&
        allocation_calibration::cpp_self_check(false) && restoration_allocation::c_contract_cases();
    require(
        passed && restoration_allocation::observation_valid(),
        "one shared real C137/C++211 owner and actual C ABI contracts calibrate this executable");
}
inline const char* restoration_provider_route() noexcept {
#if defined(_WIN32) && defined(_DEBUG)
    return "static_debug_CRT_malloc_dbg_free_dbg";
#elifdef _WIN32
    return "static_CRT_malloc_base_free_base";
#elifdef __linux__
    return "ELF_wrap_real_malloc_free";
#elifdef __APPLE__
    return "dyld_typed_interpose_original_malloc_free";
#else
    return "unqualified_platform_provider";
#endif
}
struct RestorationPayloadVisibility {
    std::size_t real = 0, spectrum = 0, sums = 0, observations = 0;
    [[nodiscard]] std::size_t observed_payload() const {
        return std::max({real + sums, real + (2 * spectrum), real + observations});
    }
    [[nodiscard]] std::size_t unobserved_payload(const RestorationResourceBound& bound) const {
        return std::max({
            bound.real + bound.sums - real - sums,
            bound.real + (2 * bound.spectrum) - real - (2 * spectrum),
            bound.real + bound.observations - real - observations,
        });
    }
};
inline bool restoration_payload_pattern(std::span<const std::byte> bytes) {
    return std::ranges::all_of(bytes, [](auto value) { return value == std::byte{0x5a}; });
}
inline std::size_t restoration_owned_visibility(std::size_t payload_bytes) {
    core::Budget budget{restoration_resource_limit};
    const auto baseline = budget.used();
    std::size_t acquired = 0;
    bool usable = false;
    {
        const RestorationObservationScope scope{0};
        {
            auto buffer = budget.allocate(payload_bytes);
            if (buffer) {
                std::ranges::fill(buffer->bytes(), std::byte{0x5a});
                acquired = restoration_allocation::live.load();
                usable =
                    buffer->size() == payload_bytes && restoration_payload_pattern(buffer->bytes());
            }
        }
    }
    std::cout << R"({"source_C_provider_route":")" << restoration_provider_route()
              << R"(","core_route":"Budget_allocate_scalar_aligned_nothrow64","actual_core_)"
                 R"(payload_bytes":)"
              << payload_bytes << R"(,"observed_core_heap_bytes":)" << acquired << R"(,"attempts":)"
              << restoration_allocation::allocation_attempts.load() << R"(,"c_attempts":)"
              << restoration_allocation::c_allocation_attempts.load() << R"(,"events":)";
    restoration_trace::write(std::cout);
    std::cout << "}\n";
    require(usable && budget.used() == baseline && budget.peak() == baseline + payload_bytes &&
                restoration_allocation::live.load() == 0 &&
                (acquired == 0 || acquired == payload_bytes) &&
                restoration_allocation::peak.load() == acquired &&
                restoration_allocation::allocation_attempts.load() == (acquired == 0 ? 0U : 1U) &&
                restoration_allocation::observation_valid(),
            "this actual core size/ABI route is exactly excluded or fully observed; other "
            "visibility refuses");
    return acquired;
}
inline RestorationPayloadVisibility
restoration_payload_visibility(const RestorationResourceBound& bound) {
    // R/S/U/M all use the same compiled Budget::allocate aligned-nothrow path at alignment64.
    // Observe EACH actual requested size through that owner, not a small-size/type inference.
    // Mixed exact per-size coverage stays explicit; partial/header-inclusive coverage refuses.
    return {
        .real = restoration_owned_visibility(bound.real),
        .spectrum = restoration_owned_visibility(bound.spectrum),
        .sums = restoration_owned_visibility(bound.sums),
        .observations = restoration_owned_visibility(bound.observations),
    };
}
} // namespace docenhance::tests
