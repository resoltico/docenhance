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
struct RestorationOwnedVisibility {
    std::size_t payload = 0, overhead = 0;
};
struct RestorationPayloadVisibility {
    RestorationOwnedVisibility real, spectrum, sums, observations;
    [[nodiscard]] std::size_t observed_payload() const {
        return std::max({
            real.payload + sums.payload,
            real.payload + (2 * spectrum.payload),
            real.payload + observations.payload,
        });
    }
    [[nodiscard]] std::size_t observed_overhead() const {
        return std::max({
            real.overhead + sums.overhead,
            real.overhead + (2 * spectrum.overhead),
            real.overhead + observations.overhead,
        });
    }
    [[nodiscard]] std::size_t unobserved_payload(const RestorationResourceBound& bound) const {
        return std::max({
            bound.real + bound.sums - real.payload - sums.payload,
            bound.real + (2 * bound.spectrum) - real.payload - (2 * spectrum.payload),
            bound.real + bound.observations - real.payload - observations.payload,
        });
    }
};
inline bool restoration_payload_pattern(std::span<const std::byte> bytes) {
    return std::ranges::all_of(bytes, [](auto value) { return value == std::byte{0x5a}; });
}
inline RestorationOwnedVisibility restoration_owned_visibility(std::size_t payload_bytes) {
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
    const auto overhead = acquired > payload_bytes ? acquired - payload_bytes : 0;
#if defined(_WIN32) && !defined(_DEBUG)
    // This actual Release CRT aligned64 route has one fully observed backing allocation.
    // Keep its measured alignment/pointer metadata separate from charged payload bytes.
    const bool known_backing = overhead == core::buffer_alignment + sizeof(void*) - 1;
#else
    constexpr bool known_backing = false;
#endif
    std::cout << R"({"source_C_provider_route":")" << restoration_provider_route()
              << R"(","core_route":"Budget_allocate_scalar_aligned_nothrow64","actual_core_)"
                 R"(payload_bytes":)"
              << payload_bytes << R"(,"observed_core_heap_bytes":)" << acquired
              << R"(,"core_backing_overhead_bytes":)" << overhead << R"(,"attempts":)"
              << restoration_allocation::allocation_attempts.load() << R"(,"c_attempts":)"
              << restoration_allocation::c_allocation_attempts.load() << R"(,"events":)";
    restoration_trace::write(std::cout);
    std::cout << "}\n";
    require(
        usable && budget.used() == baseline && budget.peak() == baseline + payload_bytes &&
            restoration_allocation::live.load() == 0 &&
            (acquired == 0 || acquired == payload_bytes || known_backing) &&
            restoration_allocation::peak.load() == acquired &&
            restoration_allocation::allocation_attempts.load() == (acquired == 0 ? 0U : 1U) &&
            (!known_backing || restoration_allocation::c_allocation_attempts.load() == 1) &&
            restoration_allocation::observation_valid(),
        "this actual core route is excluded, exact payload or known full backing; other visibility "
        "refuses");
    return {.payload = acquired == 0 ? 0 : payload_bytes, .overhead = overhead};
}
inline RestorationPayloadVisibility
restoration_payload_visibility(const RestorationResourceBound& bound) {
    // R/S/U/M all use the same compiled Budget::allocate aligned-nothrow path at alignment64.
    // Observe EACH actual requested size through that owner, not a small-size/type inference.
    // Exact payload and known raw-backing metadata stay separate; other partial routes refuse.
    return {
        .real = restoration_owned_visibility(bound.real),
        .spectrum = restoration_owned_visibility(bound.spectrum),
        .sums = restoration_owned_visibility(bound.sums),
        .observations = restoration_owned_visibility(bound.observations),
    };
}
} // namespace docenhance::tests
