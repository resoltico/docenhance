// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/gaussian.hpp"
#include "docenhance/image/plane.hpp"
#include "gamma_fixture.hpp"
#include "require.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stop_token>
#include <vector>

namespace docenhance::tests {
inline core::Result<void> gaussian_bands(image::PlaneView<const double> input,
                                         image::PlaneView<double> output, image::GaussianPass pass,
                                         const exec::Scheduler& scheduler) {
    const auto bands = std::min(std::size_t{64}, static_cast<std::size_t>(input.height()));
    const auto task = [&](std::size_t band) -> core::Result<void> {
        auto descriptor = pass;
        descriptor.first_row =
            static_cast<std::uint32_t>(std::uint64_t{band} * input.height() / bands);
        descriptor.last_row =
            static_cast<std::uint32_t>(std::uint64_t{band + 1} * input.height() / bands);
        return image::gaussian_pass(input, output, descriptor, scheduler.cancellation());
    };
    return scheduler.for_each(bands, exec::WorkRef{task});
}
inline void gaussian_band_workers_case(bool horizontal) {
    core::Budget budget{std::size_t{2} * 1024 * 1024};
    auto input = image::Plane<double>::allocate(budget, 33, 137).value();
    auto full = image::Plane<double>::allocate(budget, 33, 137).value();
    auto output = image::Plane<double>::allocate(budget, 33, 137).value();
    std::ranges::fill(input.view().storage(), -1);
    std::ranges::fill(full.view().storage(), -2);
    for (std::uint32_t y = 0; y < input.height(); ++y) {
        for (std::uint32_t x = 0; x < input.width(); ++x) {
            input.view().row(y).subspan(x, 1).front() =
                static_cast<double>((x + (13 * y)) % 997) / 1000;
        }
    }
    const auto original = gamma_bits(input.view().storage());
    const std::array<double, 3> weights{0.5, 1, 0.5};
    const image::GaussianPass pass{
        .weights = weights,
        .normalization = 2,
        .horizontal = horizontal,
        .first_row = 0,
        .last_row = input.height(),
    };
    require(image::gaussian_pass(input.view().as_const(), full.view(), pass).has_value(),
            "actual canonical full explicit Gaussian pass succeeds");
    for (std::uint32_t y = 0; y < full.height(); ++y) {
        require(std::ranges::all_of(
                    full.view().row(y),
                    [](double value) { return std::isfinite(value) && value >= 0 && value <= 1; }),
                "canonical full pass writes finite Unit data at every logical destination");
    }
    for (const unsigned workers : {1U, 2U, 7U}) {
        std::ranges::fill(output.view().storage(), -2);
        const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
        require(gaussian_bands(input.view().as_const(), output.view(), pass, scheduler).has_value(),
                "actual disjoint output row bands succeed");
        require(
            gamma_bits(full.view().storage()) == gamma_bits(output.view().storage()) &&
                gamma_bits(input.view().storage()) == original,
            "whole and1/2/7 Gaussian bands match all output/padding bits without mutating input");
    }
}
inline void gaussian_partial_band_case() {
    core::Budget budget{1024};
    auto input = image::Plane<double>::allocate(budget, 3, 3).value();
    auto output = image::Plane<double>::allocate(budget, 3, 3).value();
    std::ranges::fill(input.view().storage(), 0.4);
    std::ranges::fill(output.view().storage(), -1);
    const std::array<double, 3> weights{0.5, 1, 0.5};
    require(image::gaussian_pass(input.view().as_const(), output.view(),
                                 {
                                     .weights = weights,
                                     .normalization = 2,
                                     .first_row = 1,
                                     .last_row = 2,
                                 })
                .has_value(),
            "one interior row band is admitted against full immutable input");
    const auto view = output.view();
    for (std::uint32_t y = 0; y < view.height(); ++y) {
        for (std::size_t x = 0; x < view.row_pitch(); ++x) {
            const auto value =
                view.storage().subspan((std::size_t{y} * view.row_pitch()) + x, 1).front();
            require(
                value == (y == 1 && x < view.width() ? 0.4 : -1),
                "a partial pass touches only its logical output row, never padding or other rows");
        }
    }
}
inline void gaussian_row_admission_cases() {
    core::Budget budget{1024};
    auto input = image::Plane<double>::allocate(budget, 3, 2).value();
    auto output = image::Plane<double>::allocate(budget, 3, 2).value();
    std::ranges::fill(input.view().storage(), 0.4);
    const std::array<double, 3> weights{0.5, 1, 0.5};
    std::stop_source stop;
    request_stop(stop);
    const core::Cancellation cancelled{stop.get_token()};
    for (const auto rows : {std::array{0U, 0U}, std::array{1U, 0U}, std::array{0U, 3U}}) {
        const auto refused = image::gaussian_pass(input.view().as_const(), output.view(),
                                                  {
                                                      .weights = weights,
                                                      .normalization = 2,
                                                      .first_row = rows.front(),
                                                      .last_row = rows.back(),
                                                  },
                                                  cancelled);
        require(!refused && refused.error().code == core::ErrorCode::argument,
                "bad explicit row bounds fail before processing cancellation");
    }
    const auto valid_stopped = image::gaussian_pass(input.view().as_const(), output.view(),
                                                    {
                                                        .weights = weights,
                                                        .normalization = 2,
                                                        .first_row = 0,
                                                        .last_row = 2,
                                                    },
                                                    cancelled);
    require(!valid_stopped && valid_stopped.error().code == core::ErrorCode::cancelled,
            "valid admitted pass still observes processing cancellation");
}
inline void gaussian_support_cases() {
    core::Budget budget{1024};
    auto input = image::Plane<double>::allocate(budget, 1, 1).value();
    auto output = image::Plane<double>::allocate(budget, 1, 1).value();
    input.view().row(0).front() = 0.4;
    std::vector<double> maximum(769, 1);
    require(image::gaussian_pass(input.view().as_const(), output.view(),
                                 {
                                     .weights = maximum,
                                     .normalization = 769,
                                     .first_row = 0,
                                     .last_row = 1,
                                 })
                    .has_value() &&
                output.view().row(0).front() == 0.4,
            "shared Gaussian retains I02 maximum769-coefficient support and singleton constant "
            "identity");
    std::vector<double> excessive(771, 1);
    require(!image::gaussian_pass(input.view().as_const(), output.view(),
                                  {
                                      .weights = excessive,
                                      .normalization = 771,
                                      .first_row = 0,
                                      .last_row = 1,
                                  }),
            "support beyond the real shared bound refuses");
    const std::array<double, 3> zero_weight{0, 1, 0.5};
    require(!image::gaussian_pass(input.view().as_const(), output.view(),
                                  {
                                      .weights = zero_weight,
                                      .normalization = 1.5,
                                      .first_row = 0,
                                      .last_row = 1,
                                  }),
            "nonpositive coefficients remain actual per-pass refusals");
}
inline void gaussian_metadata_cases() {
    core::Budget budget{1024};
    auto input = image::Plane<double>::allocate(budget, 1, 1).value();
    auto output = image::Plane<double>::allocate(budget, 1, 1).value();
    input.view().row(0).front() = 0.4;
    std::ranges::fill(output.view().storage(), -1);
    const auto original = gamma_bits(output.view().storage());
    const std::array<double, 3> healthy{0.5, 1, 0.5};
    for (const auto bad : {
             std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(),
             0.0,
             -1.0,
         }) {
        const auto normalization = image::gaussian_pass(input.view().as_const(), output.view(),
                                                        {
                                                            .weights = healthy,
                                                            .normalization = bad,
                                                            .first_row = 0,
                                                            .last_row = 1,
                                                        });
        require(!normalization && normalization.error().code == core::ErrorCode::argument,
                "nonfinite or nonpositive normalization is rejected even on constant input");
        const std::array<double, 3> weights{bad, 1, 0.5};
        const auto coefficient_total = bad == -1.0 ? 0.5 : 2.0;
        const auto coefficient = image::gaussian_pass(input.view().as_const(), output.view(),
                                                      {
                                                          .weights = weights,
                                                          .normalization = coefficient_total,
                                                          .first_row = 0,
                                                          .last_row = 1,
                                                      });
        require(!coefficient && coefficient.error().code == core::ErrorCode::argument &&
                    gamma_bits(output.view().storage()) == original,
                "nonfinite or nonpositive weight cannot bypass metadata admission via constant "
                "identity");
    }
}
inline void gaussian_partial_alias_cases() {
    std::array<double, 9> shared{};
    std::ranges::fill(shared, 0.4);
    const image::PlaneShape shape{.width = 3, .height = 2, .stride = 3 * sizeof(double)};
    const auto input =
        image::PlaneView<const double>::create(std::span{shared}.first(6), shape).value();
    const auto output =
        image::PlaneView<double>::create(std::span{shared}.subspan(3, 6), shape).value();
    const auto before = gamma_bits(shared);
    const std::array<double, 3> weights{0.5, 1, 0.5};
    const auto refused = image::gaussian_pass(input, output,
                                              {
                                                  .weights = weights,
                                                  .normalization = 2,
                                                  .first_row = 1,
                                                  .last_row = 2,
                                              });
    require(!refused && refused.error().code == core::ErrorCode::argument &&
                gamma_bits(shared) == before,
            "partial output row disjointness cannot relax overlapping full input/output roles");
    std::array<double, 6> source{};
    std::array<double, 6> destination{};
    std::ranges::fill(source, 0.4);
    std::ranges::fill(destination, 0.5);
    const auto separate = image::PlaneView<const double>::create(source, shape).value();
    const auto target = image::PlaneView<double>::create(destination, shape).value();
    const auto original_source = gamma_bits(source);
    const auto original_target = gamma_bits(destination);
    const auto alias = image::gaussian_pass(separate, target,
                                            {
                                                .weights = target.row(0),
                                                .normalization = 1.5,
                                                .first_row = 1,
                                                .last_row = 2,
                                            });
    require(
        !alias && alias.error().code == core::ErrorCode::argument &&
            gamma_bits(source) == original_source && gamma_bits(destination) == original_target,
        "coefficients in an inactive output row still alias the full output role and must refuse");
}
inline void gaussian_band_cases() {
    gaussian_band_workers_case(true);
    gaussian_band_workers_case(false);
    gaussian_partial_band_case();
    gaussian_row_admission_cases();
    gaussian_support_cases();
    gaussian_metadata_cases();
    gaussian_partial_alias_cases();
}
} // namespace docenhance::tests
