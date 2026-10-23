// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "allocation_observer.hpp"
#include "nlm_launch_gate.hpp"
#include "nlm_resource_cases.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <new>

namespace docenhance::tests {
namespace nlm_allocation = allocation_observer;
inline void nlm_warm_exception_runtime() {
    bool caught = false;
    try {
        throw std::bad_alloc{};
    } catch (const std::bad_alloc&) {
        caught = true;
    }
    require(caught, "warmup contains its actual allocation exception before observation");
}
struct NlmObservedCall {
    std::optional<methods::NlmFacts> facts;
    std::optional<core::ErrorCode> error;
    std::size_t peak = 0;
    std::size_t attempts = 0;
    std::size_t live = 0;
    std::size_t workers = 0;
    std::size_t gather_arrivals = 0;
    std::size_t mapping_arrivals = 0;
};
class NlmObservationScope {
  public:
    explicit NlmObservationScope(std::size_t failure) {
        nlm_allocation::reset_workers();
        nlm_allocation::peak.store(0);
        nlm_allocation::allocation_attempts.store(0);
        nlm_allocation::c_allocation_attempts.store(0);
        nlm_allocation::persistent_failure.store(false);
        nlm_allocation::failure_at.store(failure);
        nlm_allocation::observing.store(true);
    }
    ~NlmObservationScope() {
        nlm_allocation::observing.store(false);
        nlm_allocation::failure_at.store(0);
        nlm_allocation::persistent_failure.store(false);
    }
    NlmObservationScope(const NlmObservationScope&) = delete;
    NlmObservationScope& operator=(const NlmObservationScope&) = delete;
    NlmObservationScope(NlmObservationScope&&) = delete;
    NlmObservationScope& operator=(NlmObservationScope&&) = delete;
};
inline void nlm_observer_calibration() {
    bool acquired = false;
    {
        const NlmObservationScope scope{0};
        {
            using New = void* (*)(std::size_t);
            using Delete = void (*)(void*) noexcept;
            const volatile auto allocate = static_cast<New>(&::operator new);
            const volatile auto release = static_cast<Delete>(&::operator delete);
            const auto deallocate = release;
            const std::unique_ptr<void, Delete> bytes{allocate(211), deallocate};
            acquired = bytes && nlm_allocation::live.load() == 211;
        }
    }
    require(acquired && nlm_allocation::live.load() == 0 &&
                nlm_allocation::allocation_attempts.load() == 1 &&
                nlm_allocation::peak.load() == 211 && nlm_allocation::observation_valid(),
            "ordinary allocation observer self-calibrates live, peak, attempt and release");
}
inline NlmObservedCall nlm_observe(NlmWorkingFixture& fixture, unsigned workers,
                                   std::size_t failure) {
    const auto concurrency = exec::Concurrency::resolve(workers, 7, 0, 0);
    require(concurrency.has_value(), "resource observation has admitted worker count");
    NlmLaunchGate launch_gate{workers};
    const exec::Scheduler scheduler{*concurrency, core::Cancellation{{}, &NlmLaunchGate::observe}};
    const auto method = methods::Nlm::create({.h = 1, .patch = 3, .search = 7});
    require(method.has_value(), "resource observation has admitted actual NLM method");
    NlmObservedCall observed;
    {
        const NlmObservationScope scope{failure};
        {
            const auto result = methods::apply(*method, {fixture.luminance.view(),
                                                         fixture.protection.view().as_const(),
                                                         fixture.budget, scheduler});
            if (result) {
                observed.facts = *result;
            } else {
                observed.error = result.error().code;
            }
        }
        // Join the pre-observation watchdog before observing counters or live owners.
        launch_gate.finish();
        // Error diagnostics must be destroyed before the live-owner observation.
        observed.peak = nlm_allocation::peak.load();
        observed.attempts = nlm_allocation::allocation_attempts.load();
        observed.live = nlm_allocation::live.load();
        observed.workers = nlm_allocation::worker_count;
        observed.gather_arrivals = launch_gate.arrivals(0);
        observed.mapping_arrivals = launch_gate.arrivals(1);
    }
    launch_gate.require_valid();
    return observed;
}
inline void nlm_print_observation(unsigned workers, std::size_t failure,
                                  const NlmObservedCall& observed) {
#if DE_ALLOCATION_SANITIZER_OBSERVATION
    constexpr auto mode = "sanitizer_heap_including_aligned";
#else
    constexpr auto mode = "ordinary_cpp_metadata_excluding_aligned";
#endif
    std::cout << R"({"mode":")" << mode << R"(","workers":)" << workers << R"(,"fail_n":)"
              << failure << R"(,"attempts":)" << observed.attempts << R"(,"peak":)" << observed.peak
              << R"(,"live_after":)" << observed.live << R"(,"observed_workers":)"
              << observed.workers << R"(,"gather_arrivals":)" << observed.gather_arrivals
              << R"(,"mapping_arrivals":)" << observed.mapping_arrivals << "}\n";
}
[[nodiscard]] inline bool nlm_observed_failures(unsigned workers) {
    NlmWorkingFixture healthy{9, 449, nlm_working_test_scratch};
    nlm_protect_fixture(healthy);
    const NlmBacking before{healthy};
    const auto reference = nlm_working_reference(healthy, {.h = 1, .patch = 3, .search = 7});
    const auto successful = nlm_observe(healthy, workers, 0);
    nlm_print_observation(workers, 0, successful);
    require(successful.facts && *successful.facts == reference.facts &&
                successful.facts->counts.changed > 0 && successful.live == 0 &&
                successful.workers <= 8 && nlm_allocation::observation_valid(),
            "healthy actual method joins and releases observed owners with full independent Facts");
    require(workers == 1 || (successful.gather_arrivals == 2 && successful.mapping_arrivals == 2),
            "healthy calibration holds both launch phases until two actual workers arrive");
    nlm_resource_complete(healthy, before, reference);
    require(workers == 1 || successful.attempts > 0,
            "real multitask scheduler must expose ordinary thread-start metadata allocations");
    bool all_attained = true;
    for (std::size_t failure = 1; failure <= successful.attempts; ++failure) {
        NlmWorkingFixture fixture{9, 449, nlm_working_test_scratch};
        nlm_protect_fixture(fixture);
        const NlmBacking entering{fixture};
        const auto observed = nlm_observe(fixture, workers, failure);
        const bool reached = observed.attempts >= failure;
        all_attained = all_attained && reached;
        require((reached ? (!observed.facts && observed.error == core::ErrorCode::resource)
                         : (observed.facts && *observed.facts == reference.facts)) &&
                    observed.live == 0 && observed.workers <= 8 &&
                    nlm_allocation::observation_valid(),
                "attained ordinary fail-N is Resource; unlaunched schedule ordinals stay healthy");
        if (reached) {
            nlm_resource_stores(fixture, entering, reference);
        } else {
            nlm_resource_complete(fixture, entering, reference);
        }
        nlm_print_observation(workers, failure, observed);
    }
    return all_attained;
}
} // namespace docenhance::tests
