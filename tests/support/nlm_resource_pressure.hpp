// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "nlm_resource_cases.hpp"

#include <stop_token>

namespace docenhance::tests {
struct NlmPressure {
    NlmPressure(core::Budget& ledger, std::size_t ordinal, bool pending)
        : budget(ledger), target(ordinal), stop_pending(pending) {
        active = this;
    }
    ~NlmPressure() {
        active = nullptr;
    }
    NlmPressure(const NlmPressure&) = delete;
    NlmPressure& operator=(const NlmPressure&) = delete;
    NlmPressure(NlmPressure&&) = delete;
    NlmPressure& operator=(NlmPressure&&) = delete;
    static bool observe(core::Checkpoint phase) noexcept {
        if (phase == core::Checkpoint::allocation && ++active->hits == active->target) {
            auto held = active->budget.reserve(active->budget.available());
            if (held) {
                active->pressure = std::move(*held);
                active->reserved = true;
            }
            if (active->stop_pending) {
                active->stop.request_stop();
            }
        }
        return false;
    }
    static inline NlmPressure* active = nullptr;
    core::Budget& budget;
    std::size_t target;
    bool stop_pending;
    std::stop_source stop;
    core::Reservation pressure;
    std::size_t hits = 0;
    bool reserved = false;
};
inline void nlm_pressure_case(std::size_t ordinal, bool pending) {
    NlmWorkingFixture fixture{7, 7, nlm_working_test_scratch};
    fixture.protection.view().row(0)[0] = 255;
    const NlmBacking before{fixture};
    NlmPressure pressure{fixture.budget, ordinal, pending};
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    require(concurrency.has_value(), "pressure observation has an admitted serial scheduler");
    const exec::Scheduler scheduler{
        *concurrency, core::Cancellation{pressure.stop.get_token(), &NlmPressure::observe}};
    const auto result = run_nlm(fixture, {.h = 1, .patch = 3, .search = 7}, scheduler);
    require(!result && result.error().code == core::ErrorCode::resource && pressure.reserved &&
                pressure.hits == ordinal && pressure.pressure.size() > 0,
            "every actual shared/band charge refuses Resource after calibrated budget pressure");
    require(!pending || pressure.stop.stop_requested(),
            "Resource precedes a stop made pending inside the same allocation checkpoint");
    nlm_resource_unchanged(fixture, before);
    require(fixture.budget.used() == before.resident + pressure.pressure.size(),
            "method owners refund while the deliberately held pressure lease remains charged");
    pressure.pressure = {};
    require(fixture.budget.used() == before.resident,
            "pressure lease release restores the true resident Y/mask baseline");
}
inline void nlm_resource_pressure() {
    for (std::size_t ordinal = 1; ordinal <= 9; ++ordinal) {
        nlm_pressure_case(ordinal, false);
        nlm_pressure_case(ordinal, true);
    }
}
} // namespace docenhance::tests
