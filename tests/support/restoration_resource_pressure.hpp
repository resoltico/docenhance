// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "restoration_resource_fixture.hpp"

#include <stop_token>

namespace docenhance::tests {
struct RestorationPressure {
    RestorationPressure(core::Budget& owner, std::size_t ordinal, bool pending)
        : budget(owner), target(ordinal), pending_stop(pending) {
        active = this;
    }
    ~RestorationPressure() {
        active = nullptr;
    }
    RestorationPressure(const RestorationPressure&) = delete;
    RestorationPressure& operator=(const RestorationPressure&) = delete;
    RestorationPressure(RestorationPressure&&) = delete;
    RestorationPressure& operator=(RestorationPressure&&) = delete;
    static bool observe(core::Checkpoint phase) noexcept {
        if (phase == core::Checkpoint::allocation && ++active->hits == active->target) {
            auto result = active->budget.get().reserve(active->budget.get().available());
            if (result) {
                active->held = std::move(*result);
                active->reserved = true;
            }
            if (active->pending_stop) {
                active->stop.request_stop();
            }
        }
        return false;
    }
    static inline RestorationPressure* active = nullptr;
    std::reference_wrapper<core::Budget> budget;
    std::size_t target;
    bool pending_stop;
    std::stop_source stop;
    core::Reservation held;
    std::size_t hits = 0;
    bool reserved = false;
};
inline void restoration_pressure_case(image::Extent extent, std::size_t ordinal, bool pending) {
    core::Budget budget{restoration_resource_limit};
    RestorationResourceFixture fixture{budget, extent};
    const auto method = resource_wiener();
    const auto psf = resource_psf();
    const auto coefficients = psf.coefficients;
    const auto baseline = budget.used();
    RestorationPressure pressure{budget, ordinal, pending};
    const exec::Scheduler scheduler{
        resource_concurrency(),
        core::Cancellation{pressure.stop.get_token(), &RestorationPressure::observe}};
    const auto result = methods::apply(method, psf, fixture.working(scheduler));
    if (ordinal == 0) {
        require(result.has_value() && pressure.hits == 12,
                "healthy complete-method trace attains all twelve actual serial acquisitions");
        resource_facts(*result, extent);
        fixture.preserved(false);
        require(budget.peak() == baseline + resource_bound(extent).peak,
                "actual charged high water matches the simultaneous phase lifetime calculation");
    } else {
        require(
            !result && result.error().code == core::ErrorCode::resource && pressure.reserved &&
                pressure.hits == ordinal && pressure.held.size() > 0,
            "each actual full-method acquisition is reached and refuses the real budget charge");
        fixture.preserved(true);
        require(!pending || pressure.stop.stop_requested(),
                "actual Resource charge refusal precedes the stop newly pending in its checkpoint");
    }
    require(psf.coefficients == coefficients && budget.used() == baseline + pressure.held.size(),
            "PSF remains immutable and every method owner refunds while pressure remains charged");
    pressure.held = {};
    require(budget.used() == baseline, "pressure release restores actual resident baseline");
}
inline void restoration_pressure_cases(image::Extent extent) {
    restoration_pressure_case(extent, 0, false);
    for (std::size_t ordinal = 1; ordinal <= 12; ++ordinal) {
        restoration_pressure_case(extent, ordinal, false);
        restoration_pressure_case(extent, ordinal, true);
    }
}
inline void restoration_exact_budget(image::Extent extent) {
    const auto capacity =
        gamma_fixture_bytes(extent.width, extent.height) + resource_bound(extent).peak;
    const auto method = resource_wiener();
    const auto psf = resource_psf();
    const exec::Scheduler scheduler{resource_concurrency()};
    for (const auto missing : {std::size_t{0}, std::size_t{1}}) {
        core::Budget budget{capacity - missing};
        RestorationResourceFixture fixture{budget, extent};
        const auto baseline = budget.used();
        const auto result = methods::apply(method, psf, fixture.working(scheduler));
        require(budget.used() == baseline, "exact and one-byte-short method owners fully refund");
        if (missing == 0) {
            require(result.has_value() && budget.peak() == capacity,
                    "exact simultaneous payload and FFT phase capacity suffices");
            resource_facts(*result, extent);
            fixture.preserved(false);
        } else {
            require(!result && result.error().code == core::ErrorCode::resource,
                    "one byte less refuses the actual peak acquisition before map");
            fixture.preserved(true);
        }
    }
}
} // namespace docenhance::tests
