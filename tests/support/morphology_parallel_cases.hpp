// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/morphology_working.hpp"
#include "docenhance/methods/parameters.hpp"
#include "gamma_fixture.hpp"
#include "morphology_kernel_cases.hpp"
#include "morphology_reference.hpp"
#include "morphology_working_cases.hpp"
#include "morphology_working_fixture.hpp"
#include "morphology_working_reference.hpp"
#include "require.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace docenhance::tests {
inline std::vector<double> morphology_line_gaussian_reference(std::span<const double> input) {
    std::vector<double> result(input.size());
    for (std::uint32_t i = 0; i < input.size(); ++i) {
        bool constant = true;
        double numerator = 0;
        double denominator = 0;
        for (std::int64_t offset = -384; offset <= 384; ++offset) {
            const auto value = input[morphology_reflect(std::int64_t{i} + offset,
                                                        static_cast<std::uint32_t>(input.size()))];
            constant = constant && value == input[i];
            const auto weight = std::exp(-static_cast<double>(offset * offset) / 32768);
            numerator += value * weight;
            denominator += weight;
        }
        result.at(i) = constant ? input[i] : numerator / denominator;
    }
    return result;
}
inline void morphology_maximum_worker_case(bool vertical) {
    std::vector<double> input;
    input.reserve(morphology_baseline_width);
    for (std::uint32_t i = 0; i < morphology_baseline_width; ++i) {
        input.push_back(morphology_baseline_sample(i));
    }
    const auto closing = morphology_line_reference(morphology_line_reference(input, true), false);
    const auto background = morphology_line_gaussian_reference(closing);
    std::vector<std::uint64_t> original;
    std::optional<methods::MorphologyFacts> facts;
    for (const auto workers : {1U, 2U, 7U}) {
        MorphologyWorkingFixture fixture{vertical ? 1U : morphology_baseline_width,
                                         vertical ? morphology_baseline_width : 1U,
                                         morphology_test_scratch};
        for (std::uint32_t i = 0; i < morphology_baseline_width; ++i) {
            morphology_line_at(fixture.luminance.view(), i) = input.at(i);
        }
        MorphologyWorkingReference expected;
        auto entering = morphology_reference_input(fixture, expected);
        const auto selection = morphology_reference_selection(fixture, entering);
        expected.facts.resolved_radius = 256;
        expected.facts.sigma = 128;
        expected.facts.gaussian_radius = 384;
        expected.facts.analysis = morphology_reference_statistics(selection, background, 0.9);
        const methods::MorphologyParameters p{.max_gain = 4, .target = 0.9, .radius = 256};
        morphology_reference_map(fixture, background, p, expected);
        const auto held = fixture.budget.used();
        const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
        const auto actual =
            methods::apply(methods::Morphology::create(p).value(),
                           {fixture.luminance.view(), fixture.protection.view().as_const(),
                            fixture.budget, scheduler});
        require(actual && actual->analysis && expected.facts.analysis &&
                    methods::validate(*actual, p,
                                      {
                                          .width = fixture.luminance.width(),
                                          .height = fixture.luminance.height(),
                                      }) &&
                    actual->counts == expected.facts.counts &&
                    actual->gain_capped_samples == expected.facts.gain_capped_samples &&
                    actual->saturated_samples == expected.facts.saturated_samples &&
                    morphology_analysis_close(*actual->analysis, *expected.facts.analysis) &&
                    fixture.budget.used() == held &&
                    fixture.budget.peak() ==
                        held + morphology_test_peak(fixture.luminance.width(),
                                                    fixture.luminance.height(), 256,
                                                    std::uint64_t{fixture.luminance.width()} *
                                                        fixture.luminance.height()),
                "maximal-radius actual task bank/field/ranks/map match independent singleton math "
                "and refund");
        const auto output = fixture.luminance.view().storage();
        for (std::size_t i = 0; i < expected.samples.size(); ++i) {
            require(std::abs(output.subspan(i, 1).front() - expected.samples.at(i)) < 1e-12,
                    "every actual maximal-support Y sample and padding agrees with independent "
                    "reference");
        }
        const auto bits = gamma_bits(output);
        if (!facts) {
            facts = *actual;
            original = bits;
        } else {
            require(*actual == *facts && bits == original,
                    "1/2/7 workers give bit-identical maximal-bank axis/seam Y and complete Facts");
        }
    }
}
inline void morphology_interval_cases() {
    core::Budget budget{16384};
    auto input = image::Plane<double>::allocate(budget, 3, 3).value();
    auto output = image::Plane<double>::allocate(budget, 3, 3).value();
    std::ranges::fill(input.view().storage(), 0.25);
    std::ranges::fill(output.view().storage(), -1);
    std::array<std::uint64_t, 515> queue{};
    const auto before = gamma_bits(output.view().storage());
    for (const auto pass : {
             methods::ExtremaPass{.radius = 1, .first_line = 1, .last_line = 1},
             methods::ExtremaPass{.radius = 1, .first_line = 2, .last_line = 1},
             methods::ExtremaPass{.radius = 1, .first_line = 0, .last_line = 4},
             methods::ExtremaPass{.radius = 0, .first_line = 0, .last_line = 3},
             methods::ExtremaPass{.radius = 257, .first_line = 0, .last_line = 3},
         }) {
        const CheckpointStop stop{core::Checkpoint::processing, 0};
        const auto result = methods::extrema_pass(input.view().as_const(), output.view(), queue,
                                                  pass, stop.cancellation());
        require(!result && result.error().code == core::ErrorCode::argument &&
                    CheckpointStop::visits() == 0 && gamma_bits(output.view().storage()) == before,
                "empty reversed out-of-axis and radius metadata refuse before a pending processing "
                "stop");
    }
    const methods::ExtremaPass partial{.radius = 1, .first_line = 1, .last_line = 2};
    const CheckpointStop stop{core::Checkpoint::processing, 0};
    const auto alias = methods::extrema_pass(input.view().as_const(), input.view(), queue, partial,
                                             stop.cancellation());
    require(!alias && alias.error().code == core::ErrorCode::argument &&
                CheckpointStop::visits() == 0,
            "otherwise-valid partial metadata retains full-role source/output alias refusal");
    require(
        methods::extrema_pass(input.view().as_const(), output.view(), queue, partial).has_value(),
        "healthy real partial interval executes its nonempty middle row");
    require(output.view().row(0).front() == -1 && output.view().row(1).front() == 0.25 &&
                output.view().row(2).front() == -1,
            "successful partial interval touches only its explicit logical rows");
}
} // namespace docenhance::tests
