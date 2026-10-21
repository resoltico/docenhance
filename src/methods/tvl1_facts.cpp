// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/tvl1_facts.hpp"

#include "docenhance/core/range.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/parameter_domains.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/tvl1.hpp"

#include <cmath>

namespace docenhance::methods {
bool validate(const Tvl1Solution& solution) noexcept {
    if (!tv_iterations_domain.contains(solution.iterations) ||
        solution.passing_checkpoints > tvl1_required_passes ||
        !core::in_range(solution.primal_update, 0.0, 1.0) ||
        !core::in_range(solution.dual_update, 0.0, 2 + tvl1_dual_roundoff) ||
        !std::isfinite(solution.objective_start) || solution.objective_start < 0 ||
        !std::isfinite(solution.objective_end) || solution.objective_end < 0 ||
        (solution.iterations < tvl1_first_checkpoint && solution.passing_checkpoints != 0)) {
        return false;
    }
    constexpr auto earliest =
        tvl1_first_checkpoint + ((tvl1_required_passes - 1) * tvl1_checkpoint_period);
    switch (solution.stop) {
    case Tvl1Stop::tolerance_met:
        return solution.iterations >= earliest &&
               solution.iterations % tvl1_checkpoint_period == 0 &&
               solution.passing_checkpoints == tvl1_required_passes;
    case Tvl1Stop::iteration_limit:
        return solution.passing_checkpoints < tvl1_required_passes;
    }
    return false;
}
namespace {
bool empty_application(const Tvl1Facts& facts) noexcept {
    return facts.counts.evaluated == 0 && facts.counts.changed == 0 && facts.context_samples == 0 &&
           facts.corrected_samples == 0 && !facts.solver;
}
bool active_application(const Tvl1Facts& facts) noexcept {
    if (!facts.solver || !validate(*facts.solver) || facts.counts.eligible == 0 ||
        facts.counts.evaluated != facts.counts.eligible ||
        facts.context_samples != facts.counts.eligible + facts.counts.protected_samples) {
        return false;
    }
    const auto samples = static_cast<double>(facts.context_samples);
    const auto& solution = *facts.solver;
    if (solution.objective_start > 2 * samples ||
        solution.objective_end > (2 + std::ceil(tv_lambda_domain.maximum)) * samples ||
        (facts.counts.protected_samples == 0 && facts.corrected_samples == 0 &&
         solution.objective_end != solution.objective_start)) {
        return false;
    }
    if (facts.context_samples == 1 || solution.objective_start == 0) {
        if (facts.corrected_samples != 0 || solution.primal_update != 0 ||
            solution.dual_update != 0 || solution.objective_start != 0 ||
            solution.objective_end != 0) {
            return false;
        }
        constexpr auto earliest =
            tvl1_first_checkpoint + ((tvl1_required_passes - 1) * tvl1_checkpoint_period);
        return solution.stop == Tvl1Stop::tolerance_met
                   ? solution.iterations == earliest
                   : solution.iterations < earliest &&
                         solution.passing_checkpoints ==
                             (solution.iterations >= tvl1_first_checkpoint ? 1U : 0U);
    }
    return true;
}
bool stopping_agrees(const Tvl1Solution& solution, Tvl1Parameters parameters) noexcept {
    if (solution.iterations > parameters.iterations) {
        return false;
    }
    if (solution.iterations >= tvl1_first_checkpoint &&
        solution.iterations % tvl1_checkpoint_period == 0) {
        const bool passed = solution.primal_update <= parameters.tolerance &&
                            solution.dual_update <= parameters.tolerance;
        if (passed != (solution.passing_checkpoints != 0)) {
            return false;
        }
    }
    return solution.stop == Tvl1Stop::iteration_limit
               ? solution.iterations == parameters.iterations
               : solution.primal_update <= parameters.tolerance &&
                     solution.dual_update <= parameters.tolerance;
}
} // namespace
bool validate(const Tvl1Facts& facts) noexcept {
    if (!validate(facts.counts) || facts.corrected_samples > facts.counts.evaluated ||
        facts.counts.changed > facts.corrected_samples) {
        return false;
    }
    switch (facts.reason) {
    case Tvl1Reason::zero_blend:
        return facts.disposition == Disposition::no_change && empty_application(facts);
    case Tvl1Reason::no_eligible_samples:
        return facts.disposition == Disposition::no_change && facts.counts.eligible == 0 &&
               empty_application(facts);
    case Tvl1Reason::none:
        return facts.disposition == Disposition::applied && facts.counts.changed != 0 &&
               active_application(facts);
    case Tvl1Reason::no_effect:
        return facts.disposition == Disposition::no_change && facts.counts.changed == 0 &&
               active_application(facts);
    }
    return false;
}
bool validate(const Tvl1Facts& facts, Tvl1Parameters parameters) noexcept {
    if (!validate(facts) || !valid_parameters(parameters)) {
        return false;
    }
    if (parameters.blend == 0) {
        return facts.reason == Tvl1Reason::zero_blend;
    }
    if (facts.counts.eligible == 0) {
        return facts.reason == Tvl1Reason::no_eligible_samples;
    }
    if (!facts.solver || !stopping_agrees(*facts.solver, parameters) ||
        facts.solver->objective_end >
            (2 + std::ceil(parameters.lambda)) * static_cast<double>(facts.context_samples) ||
        (parameters.blend == 1 && facts.counts.changed != facts.corrected_samples)) {
        return false;
    }
    return facts.reason == Tvl1Reason::none || facts.reason == Tvl1Reason::no_effect;
}
} // namespace docenhance::methods
