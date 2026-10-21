// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "restoration_resource_calibration.hpp"

#include <atomic>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace docenhance::tests {
struct RestorationObservedCall {
    std::optional<methods::WienerFacts> facts;
    std::optional<core::ErrorCode> error;
    std::size_t peak = 0, attempts = 0, c_attempts = 0, live = 0, workers = 0;
    std::size_t resident = 0, ledger_peak_before = 0, ledger_peak_after = 0;
};
inline RestorationObservedCall restoration_observe(RestorationResourceFixture& fixture,
                                                   const methods::Wiener& method,
                                                   const methods::ResolvedPsf& psf,
                                                   const exec::Scheduler& scheduler,
                                                   std::size_t failure) {
    RestorationObservedCall observed;
    observed.resident = fixture.budget.get().used();
    observed.ledger_peak_before = fixture.budget.get().peak();
    {
        const RestorationObservationScope scope{failure};
        {
            const auto result = methods::apply(method, psf, fixture.working(scheduler));
            if (result) {
                observed.facts = *result;
            } else {
                observed.error = result.error().code;
            }
        }
        // Returned diagnostics and all actual method/FFT owners have died before sampling live.
        observed.peak = restoration_allocation::peak.load();
        observed.attempts = restoration_allocation::allocation_attempts.load();
        observed.c_attempts = restoration_allocation::c_allocation_attempts.load();
        observed.live = restoration_allocation::live.load();
        observed.workers = restoration_allocation::worker_count;
        observed.ledger_peak_after = fixture.budget.get().peak();
    }
    return observed;
}
inline void restoration_observation_report(image::Extent extent, std::size_t failure,
                                           const RestorationObservedCall& observed,
                                           const RestorationResourceBound& bound,
                                           const RestorationPayloadVisibility& visibility) {
    std::cout << R"({"whole_method":true,"mode":"ordinary_cpp_and_registered_c","width":)"
              << extent.width << R"(,"height":)" << extent.height << R"(,"fft_width":)"
              << bound.transform.width << R"(,"fft_height":)" << bound.transform.height
              << R"(,"native_lanes":)" << fft::native_lanes() << R"(,"row_capacity":)"
              << bound.row_capacity << R"(,"column_capacity":)" << bound.column_capacity
              << R"(,"fail_n":)" << failure << R"(,"attained":)"
              << (failure != 0 && observed.attempts >= failure) << R"(,"attempts":)"
              << observed.attempts << R"(,"c_attempts":)" << observed.c_attempts
              << R"(,"observed_peak":)" << observed.peak << R"(,"live_after":)" << observed.live
              << R"(,"observed_core_payload_envelope":)" << visibility.observed_payload()
              << R"(,"unobserved_core_payload_envelope":)" << visibility.unobserved_payload(bound)
              << R"(,"payload_envelope":)" << bound.payload << R"(,"native_capacity":)"
              << bound.native << R"(,"source_scratch_peak_candidate":)" << bound.peak
              << R"(,"entering_charged":)" << observed.resident << R"(,"ledger_peak_before":)"
              << observed.ledger_peak_before << R"(,"ledger_peak_after":)"
              << observed.ledger_peak_after << R"(,"events":)";
    restoration_trace::write(std::cout);
    std::cout << "}\n";
}
inline void restoration_warm_application(const methods::Wiener& method,
                                         const methods::ResolvedPsf& psf,
                                         const exec::Scheduler& scheduler, image::Extent extent) {
    // A separate actual warm application cannot mutate the measured entering frame.
    {
        core::Budget warm_budget{restoration_resource_limit};
        RestorationResourceFixture warm{warm_budget, extent};
        const auto result = methods::apply(method, psf, warm.working(scheduler));
        require(result.has_value(), "original warm whole-method application completes");
    }
}
inline void restoration_observed_case(image::Extent extent, const unsigned shard,
                                      const unsigned shards) {
    const auto method = resource_wiener();
    const auto psf = resource_psf();
    const auto coefficients = psf.coefficients;
    const exec::Scheduler scheduler{resource_concurrency()};
    const auto bound = resource_bound(extent);
    const auto visibility = restoration_payload_visibility(bound);
    restoration_warm_application(method, psf, scheduler, extent);
    core::Budget budget{restoration_resource_limit};
    RestorationResourceFixture fixture{budget, extent};
    const auto baseline = budget.used();
    const auto healthy = restoration_observe(fixture, method, psf, scheduler, 0);
    require(healthy.facts && healthy.attempts > 0 && healthy.c_attempts > 0 && healthy.live == 0 &&
                healthy.workers <= 8 && budget.used() == baseline &&
                restoration_allocation::observation_valid(),
            "actual whole method reaches both provider families and refunds all observed owners");
    if (!healthy.facts.has_value()) {
        throw std::logic_error("Required healthy restoration Facts are absent");
    }
    resource_facts(*healthy.facts, extent);
    fixture.preserved(false);
    const auto completed = gamma_bits(fixture.luminance.view().storage());
    const auto conservative = healthy.peak + visibility.unobserved_payload(bound);
    require(
        healthy.peak <= bound.native + visibility.observed_payload() &&
            conservative <= bound.payload + bound.native && budget.peak() == baseline + bound.peak,
        "actual observed family mode and charged phase peaks satisfy distinct source envelopes");
    restoration_observation_report(extent, 0, healthy, bound, visibility);
    require(shards > 0 && shard < shards && healthy.attempts >= shards,
            "each compiled shard has actual calibrated failure work for this extent");
    bool all_attained = true;
    std::size_t executed = 0;
    for (std::size_t failure = 1; failure <= healthy.attempts; ++failure) {
        if ((failure - 1) % shards != shard) {
            continue;
        }
        ++executed;
        RestorationResourceFixture failed{budget, extent};
        const auto held = budget.used();
        const auto observed = restoration_observe(failed, method, psf, scheduler, failure);
        const bool attained = observed.attempts >= failure;
        all_attained = all_attained && attained;
        require((attained ? (!observed.facts && observed.error == core::ErrorCode::resource)
                          : (observed.facts && *observed.facts == *healthy.facts)) &&
                    observed.live == 0 && observed.workers <= 8 && budget.used() == held &&
                    psf.coefficients == coefficients && restoration_allocation::observation_valid(),
                "every attained actual ordinary/C fail-N is Resource with exact full refunds");
        failed.preserved(attained);
        if (!attained) {
            require(gamma_bits(failed.luminance.view().storage()) == completed,
                    "an unattained ordinal is reported honestly with coherent whole-method output");
        }
        restoration_observation_report(extent, failure, observed, bound, visibility);
    }
    require(all_attained,
            "every required calibrated full-method fail-N ordinal must actually be reached");
    require(executed > 0, "each actual sweep process executes failure observations");
    std::cout << R"({"restoration_sweep_complete":true,"width":)" << extent.width << R"(,"height":)"
              << extent.height << R"(,"healthy_attempts":)" << healthy.attempts
              << R"(,"executed_failures":)" << executed << "}\n";
}
struct RestorationNativeStop {
    static inline std::atomic<std::size_t> target{0}, entries{0}, late_visits{0}, first_attempts{0};
    static bool observe(core::Checkpoint phase) noexcept {
        if (target.load() != 0 && restoration_allocation::observing.load() &&
            restoration_allocation::allocation_attempts.load() >= target.load()) {
            late_visits.fetch_add(1);
            return true;
        }
        if (phase == core::Checkpoint::solving && entries.fetch_add(1) == 0) {
            first_attempts.store(restoration_allocation::allocation_attempts.load());
        }
        return false;
    }
};
inline void restoration_native_precedence(image::Extent extent) {
    const auto method = resource_wiener();
    const auto psf = resource_psf();
    const auto coefficients = psf.coefficients;
    RestorationNativeStop::target.store(0);
    RestorationNativeStop::entries.store(0);
    RestorationNativeStop::late_visits.store(0);
    const exec::Scheduler scheduler{resource_concurrency(),
                                    core::Cancellation{{}, &RestorationNativeStop::observe}};
    {
        core::Budget calibration{restoration_resource_limit};
        RestorationResourceFixture healthy{calibration, extent};
        const auto observed = restoration_observe(healthy, method, psf, scheduler, 0);
        require(observed.facts && observed.c_attempts > 0 &&
                    RestorationNativeStop::entries.load() > 0,
                "healthy whole method calibrates the actual first native entry allocation prefix");
    }
    const auto target = RestorationNativeStop::first_attempts.load() + 1;
    RestorationNativeStop::target.store(target);
    RestorationNativeStop::entries.store(0);
    RestorationNativeStop::late_visits.store(0);
    core::Budget budget{restoration_resource_limit};
    RestorationResourceFixture fixture{budget, extent};
    const auto baseline = budget.used();
    const auto observed = restoration_observe(fixture, method, psf, scheduler, target);
    require(!observed.facts && observed.error == core::ErrorCode::resource &&
                observed.attempts >= target && RestorationNativeStop::entries.load() == 1 &&
                RestorationNativeStop::first_attempts.load() + 1 == target &&
                RestorationNativeStop::late_visits.load() == 0 && observed.live == 0 &&
                budget.used() == baseline && psf.coefficients == coefficients &&
                observed.workers <= 8 && restoration_allocation::observation_valid(),
            "complete method retains calibrated native Resource before the next pending stop poll");
    fixture.preserved(true);
    restoration_allocation::observing.store(true);
    const bool pending = scheduler.cancellation().requested(core::Checkpoint::admission);
    restoration_allocation::observing.store(false);
    require(pending, "native refusal establishes the condition a later checkpoint would observe");
    RestorationNativeStop::target.store(0);
}
} // namespace docenhance::tests
