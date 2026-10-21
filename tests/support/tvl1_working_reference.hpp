// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/tvl1.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "require.hpp"
#include "sharpen_reference.hpp"
#include "tvl1_reference.hpp"
#include "tvl1_working_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace docenhance::tests {
inline methods::Tvl1Updates tvl1_reference_step(TvReference& reference, double lambda) {
    const auto primal = reference.primal;
    const auto px = reference.px;
    const auto py = reference.py;
    reference.iterate(lambda);
    methods::Tvl1Updates updates;
    for (std::size_t i = 0; i < primal.size(); ++i) {
        updates.primal = std::max(updates.primal, std::abs(reference.primal.at(i) - primal.at(i)));
        updates.dual = std::max(
            updates.dual, std::hypot(reference.px.at(i) - px.at(i), reference.py.at(i) - py.at(i)));
    }
    return updates;
}
inline methods::Tvl1Solution tvl1_reference_solve(TvReference& reference,
                                                  methods::Tvl1Parameters p) {
    methods::Tvl1Solution solution;
    solution.objective_start = reference.objective(p.lambda);
    for (std::uint32_t i = 1; i <= p.iterations; ++i) {
        const auto updates = tvl1_reference_step(reference, p.lambda);
        solution.iterations = i;
        solution.primal_update = updates.primal;
        solution.dual_update = updates.dual;
        if (i >= 20 && i % 10 == 0) {
            const bool passed = updates.primal <= p.tolerance && updates.dual <= p.tolerance;
            solution.passing_checkpoints = passed ? solution.passing_checkpoints + 1 : 0;
            if (solution.passing_checkpoints == 2) {
                solution.stop = methods::Tvl1Stop::tolerance_met;
                break;
            }
        }
    }
    solution.objective_end = reference.objective(p.lambda);
    require(std::isfinite(solution.objective_start) && solution.objective_start >= 0 &&
                std::isfinite(solution.objective_end) && solution.objective_end >= 0,
            "independent solver objectives are finite nonnegative before comparison");
    return solution;
}
struct Tvl1WorkingReference {
    std::vector<double> samples;
    methods::Tvl1Facts facts;
};
inline void tvl1_reference_map_row(Tvl1WorkingFixture& fixture, std::uint32_t y,
                                   const TvReference& reference, double blend,
                                   Tvl1WorkingReference& result) {
    const auto view = fixture.luminance.view();
    for (std::uint32_t x = 0; x < view.width(); ++x) {
        if (fixture.protection.view().row(y).subspan(x, 1).front() != 0) {
            continue;
        }
        const auto i = (std::size_t{y} * view.width()) + x;
        const auto f = reference.input.at(i);
        const auto u = reference.primal.at(i);
        ++result.facts.counts.evaluated;
        if (u == f) {
            continue;
        }
        ++result.facts.corrected_samples;
        const auto next = ((1 - blend) * f) + (blend * u);
        if (next != f) {
            result.samples.at((std::size_t{y} * view.row_pitch()) + x) =
                sharpen_reference_srgb_decode(next);
            ++result.facts.counts.changed;
        }
    }
}
inline Tvl1WorkingReference tvl1_working_reference(Tvl1WorkingFixture& fixture,
                                                   methods::Tvl1Parameters p) {
    const auto view = fixture.luminance.view();
    Tvl1WorkingReference result;
    result.samples.assign(view.storage().begin(), view.storage().end());
    std::vector<double> input;
    input.reserve(std::size_t{view.width()} * view.height());
    const auto protection = fixture.protection.view();
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        const auto row = view.row(y);
        const auto mask = protection.row(y);
        for (std::uint32_t x = 0; x < view.width(); ++x) {
            input.push_back(sharpen_reference_srgb_encode(row.subspan(x, 1).front()));
            if (mask.subspan(x, 1).front() == 0) {
                ++result.facts.counts.eligible;
            } else {
                ++result.facts.counts.protected_samples;
            }
        }
    }
    if (p.blend == 0) {
        result.facts.reason = methods::Tvl1Reason::zero_blend;
        return result;
    }
    if (result.facts.counts.eligible == 0) {
        return result;
    }
    TvReference reference{view.width(), view.height(), input};
    result.facts.solver = tvl1_reference_solve(reference, p);
    result.facts.context_samples = input.size();
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        tvl1_reference_map_row(fixture, y, reference, p.blend, result);
    }
    result.facts.reason = result.facts.counts.changed == 0 ? methods::Tvl1Reason::no_effect
                                                           : methods::Tvl1Reason::none;
    result.facts.disposition = result.facts.counts.changed == 0 ? methods::Disposition::no_change
                                                                : methods::Disposition::applied;
    return result;
}
inline bool tvl1_solution_matches(const methods::Tvl1Solution& actual,
                                  const methods::Tvl1Solution& expected) {
    return actual.iterations == expected.iterations && actual.stop == expected.stop &&
           actual.passing_checkpoints == expected.passing_checkpoints &&
           std::abs(actual.primal_update - expected.primal_update) < 1e-12 &&
           std::abs(actual.dual_update - expected.dual_update) < 1e-12 &&
           std::abs(actual.objective_start - expected.objective_start) < 1e-10 &&
           std::abs(actual.objective_end - expected.objective_end) < 1e-10;
}
} // namespace docenhance::tests
