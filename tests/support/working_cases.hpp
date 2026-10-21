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
#include "docenhance/methods/clahe.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/gamma.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/levels.hpp"
#include "docenhance/methods/morphology_working.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/tvl1_working.hpp"
#include "docenhance/methods/unsharp.hpp"
#include "docenhance/methods/working.hpp"
#include "require.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

namespace docenhance::tests {
static_assert(std::is_same_v<std::uint8_t, unsigned char>);
static_assert(!std::is_constructible_v<methods::Working, image::PlaneView<double>,
                                       image::PlaneView<const std::uint8_t>, core::Budget&,
                                       const exec::Scheduler&&>);
inline void working_eligibility_cases() {
    core::Budget budget{0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(7, 7, 0, 0).value()};
    std::array<double, 6> samples{0, 0.25, 0.75, 0.5, 1, 0.75};
    const auto before = samples;
    const std::array<std::uint8_t, 6> mask{0, 1, 255, 0, 255, 0};
    const auto plane = image::PlaneView<double>::create(
                           samples, {.width = 2, .height = 2, .stride = 3 * sizeof(double)})
                           .value();
    const auto protection =
        image::PlaneView<const std::uint8_t>::create(mask, {.width = 2, .height = 2, .stride = 3})
            .value();
    const auto counted = methods::eligibility({plane, protection, budget, scheduler});
    require(counted.has_value(), "mask eligibility succeeds");
    require(*counted == methods::PixelCounts{.eligible = 2, .protected_samples = 2},
            "mask padding is not counted as protected pixels");
    require(methods::counts_match(*counted, 4), "eligibility covers the complete extent");
    require(samples == before, "eligibility does not write luminance");
    require(budget.used() == 0, "eligibility acquires no charged workspace");
    const auto unmasked = methods::eligibility({plane, {}, budget, scheduler});
    require(unmasked.has_value(), "unmasked eligibility succeeds");
    require(*unmasked == methods::PixelCounts{.eligible = 4},
            "an absent mask makes every pixel eligible");
}
inline void working_tvl1_alias_case(methods::Working working) {
    std::optional<methods::Tvl1Stop> observed_stop = methods::Tvl1Stop::iteration_limit;
    const auto refused =
        methods::apply(methods::Tvl1::create({.blend = 0}).value(), working, observed_stop);
    require(!refused && refused.error().code == core::ErrorCode::argument && !observed_stop,
            "actual TV-L1 refuses overlapping roles before a no-op or stop and resets reuse");
}
inline void working_morphology_alias_case(methods::Working working) {
    const auto refused =
        methods::apply(methods::Morphology::create({.strength = 0}).value(), working);
    require(!refused && refused.error().code == core::ErrorCode::argument,
            "actual morphology refuses full-role mask overlap before a no-op or stop");
}
inline void working_gamma_alias_cases(methods::Working working,
                                      image::PlaneView<const std::uint8_t> mismatched) {
    const auto gamma = methods::Gamma::create({.blend = 0}).value();
    const auto refused_alias = methods::apply(gamma, working);
    require(!refused_alias && refused_alias.error().code == core::ErrorCode::argument,
            "actual Working Gamma refuses role overlap before a no-op or cancellation");
    const auto refused_shape =
        methods::apply(gamma, {working.luminance, mismatched, working.budget, working.scheduler});
    require(!refused_shape && refused_shape.error().code == core::ErrorCode::argument,
            "actual Working Gamma refuses mismatched protection before a no-op or stop");
}
inline void working_view_cases() {
    core::Budget budget{0};
    const CheckpointStop stop{core::Checkpoint::measurement, 0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto empty = methods::eligibility({{}, {}, budget, scheduler});
    require(!empty, "empty working views are refused");
    require(empty.error().code == core::ErrorCode::argument, "view errors precede cancellation");
    constexpr std::array<double, 4> initial{0, 0.25, 0.5, 1};
    constexpr auto storage_bytes = sizeof(double) * initial.size();
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    alignas(double) unsigned char storage[storage_bytes]{};
    const std::span bytes{storage};
    auto* const typed =
        static_cast<double*>(std::memcpy(bytes.data(), initial.data(), bytes.size()));
    const std::span samples{typed, initial.size()};
    const auto plane = image::PlaneView<double>::create(
                           samples, {.width = 2, .height = 2, .stride = 2 * sizeof(double)})
                           .value();
    const std::array<std::uint8_t, 6> mask{};
    const auto mismatched =
        image::PlaneView<const std::uint8_t>::create(mask, {.width = 3, .height = 2, .stride = 3})
            .value();
    require(!methods::validate_working_views({plane, mismatched, budget, scheduler}),
            "mask geometry must match luminance");
    const auto alias = image::PlaneView<const std::uint8_t>::create(
                           std::span<const std::uint8_t>{bytes.first(sizeof(double))},
                           {.width = 2, .height = 2, .stride = 2})
                           .value();
    require(!methods::validate_working_views({plane, alias, budget, scheduler}),
            "mask and luminance backing storage must not overlap");
    working_gamma_alias_cases({plane, alias, budget, scheduler}, mismatched);
    working_tvl1_alias_case({plane, alias, budget, scheduler});
    working_morphology_alias_case({plane, alias, budget, scheduler});
    const auto unsharp_alias = methods::apply(methods::Unsharp::create({.amount = 0}).value(),
                                              {plane, alias, budget, scheduler});
    require(!unsharp_alias && unsharp_alias.error().code == core::ErrorCode::argument,
            "actual unsharp refuses overlapping roles before a no-op or stop");
    const auto clahe = methods::Clahe::create({.blend = 0}).value();
    const auto clahe_alias = methods::apply(clahe, {plane, alias, budget, scheduler});
    require(!clahe_alias && clahe_alias.error().code == core::ErrorCode::argument,
            "actual CLAHE refuses overlapping roles before a no-op or stop");
    const auto levels = methods::Levels::create({.blend = 0}).value();
    const auto levels_alias = methods::apply(levels, {plane, alias, budget, scheduler});
    require(!levels_alias && levels_alias.error().code == core::ErrorCode::argument,
            "actual Levels refuses overlapping roles before a no-op or stop");
    const auto levels_shape = methods::apply(levels, {plane, mismatched, budget, scheduler});
    require(!levels_shape && levels_shape.error().code == core::ErrorCode::argument,
            "actual Levels refuses mismatched protection before a no-op or stop");
    require(budget.used() == 0, "view admission acquires no charged workspace");
}
inline void working_cancellation_cases() {
    constexpr std::uint32_t width = 2050;
    core::Budget budget{0};
    std::array<double, width> samples{};
    const std::array<std::uint8_t, width> mask{};
    const auto plane = image::PlaneView<double>::create(
                           samples, {.width = width, .height = 1, .stride = width * sizeof(double)})
                           .value();
    const auto protection = image::PlaneView<const std::uint8_t>::create(
                                mask, {.width = width, .height = 1, .stride = width})
                                .value();
    const CheckpointStop stop{core::Checkpoint::measurement, 2};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(7, 7, 0, 0).value(),
                                    stop.cancellation()};
    const auto counted = methods::eligibility({plane, protection, budget, scheduler});
    require(!counted, "wide-row cancellation is reported");
    require(counted.error().code == core::ErrorCode::cancelled,
            "mask scan preserves cancellation identity");
    require(CheckpointStop::visits() == 3, "mask cancellation reaches an interior checkpoint");
    require(samples == std::array<double, width>{},
            "cancelled eligibility does not write luminance");
    require(budget.used() == 0, "cancelled eligibility refunds all charged workspace");
}
inline void working_count_cases() {
    const methods::PixelCounts value{
        .eligible = 6,
        .protected_samples = 2,
        .evaluated = 6,
        .changed = 3,
    };
    require(methods::validate(value), "completed pixel counts admit coherent work");
    require(methods::counts_match(value, 8), "pixel counts agree with the known canvas");
    require(!methods::counts_match(value, 7), "pixel counts cannot claim a different canvas");
    require(!methods::validate(methods::PixelCounts{}), "absent work is not a completed report");
    require(!methods::validate(methods::PixelCounts{.eligible = 1, .changed = 1}),
            "changed samples cannot exceed evaluated samples");
}
inline void working_cases() {
    working_eligibility_cases();
    working_view_cases();
    working_cancellation_cases();
    working_count_cases();
}
} // namespace docenhance::tests
