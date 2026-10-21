// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "tvl1_detail.hpp"

#include <cstdint>
#include <expected>

namespace docenhance::methods {
bool tvl1_checkpoint(Tvl1SolveState& report, const Tvl1& method) noexcept {
    if (report.iterations >= tvl1_first_checkpoint &&
        report.iterations % tvl1_checkpoint_period == 0) {
        const auto tolerance = method.parameters().tolerance;
        report.passing_checkpoints =
            report.primal_update <= tolerance && report.dual_update <= tolerance
                ? report.passing_checkpoints + 1
                : 0;
        if (report.passing_checkpoints == tvl1_required_passes) {
            report.stop = Tvl1Stop::tolerance_met;
            return true;
        }
    }
    return false;
}
core::Result<void> solve_tvl1(Tvl1Field field, const Tvl1& method, const exec::Scheduler& scheduler,
                              Tvl1SolveState& report) {
    const auto parameters = method.parameters();
    auto start = tvl1_objective(field.input, field.primal.as_const(), parameters.lambda, scheduler);
    if (!start) {
        return std::unexpected(start.error());
    }
    report.objective_start = *start;
    for (std::uint32_t iteration = 1; iteration <= parameters.iterations; ++iteration) {
        auto updates = tvl1_iteration(field, parameters.lambda, scheduler);
        if (!updates) {
            return std::unexpected(updates.error());
        }
        report.iterations = iteration;
        report.primal_update = updates->primal;
        report.dual_update = updates->dual;
        if (tvl1_checkpoint(report, method)) {
            break;
        }
    }
    if (!report.stop) {
        report.stop = Tvl1Stop::iteration_limit;
    }
    auto end = tvl1_objective(field.input, field.primal.as_const(), parameters.lambda, scheduler);
    if (!end) {
        return std::unexpected(end.error());
    }
    report.objective_end = *end;
    return {};
}
} // namespace docenhance::methods
