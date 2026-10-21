// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "binary_fact_cases.hpp"
#include "binary_input_cases.hpp"
#include "binary_numerical_cases.hpp"
#include "clahe_working_controls.hpp"
#include "contrast_fact_cases.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "gamma_working_cases.hpp"
#include "gaussian_band_cases.hpp"
#include "illumination_receiver_cases.hpp"
#include "levels_working_cases.hpp"
#include "morphology_fact_cases.hpp"
#include "morphology_kernel_cases.hpp"
#include "morphology_parallel_cases.hpp"
#include "morphology_working_cases.hpp"
#include "morphology_working_controls.hpp"
#include "nlm_fact_cases.hpp"
#include "nlm_working_cases.hpp"
#include "nlm_working_controls.hpp"
#include "require.hpp"
#include "restoration_fact_cases.hpp"
#include "sharpen_fact_cases.hpp"
#include "stage_report_cases.hpp"
#include "statistic_plane_cases.hpp"
#include "surface_fact_cases.hpp"
#include "surface_source_cases.hpp"
#include "surface_working_boundaries.hpp"
#include "surface_working_cases.hpp"
#include "surface_working_controls.hpp"
#include "tvl1_fact_cases.hpp"
#include "tvl1_parallel_cases.hpp"
#include "tvl1_working_controls.hpp"
#include "unsharp_working_controls.hpp"
#include "warning_cases.hpp"
#include "working_cases.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace docenhance::tests {
inline void ownership_cases() {
    core::Buffer survivor;
    {
        core::Budget ephemeral{512};
        survivor = std::move(ephemeral.allocate(128).value());
        survivor.bytes().front() = std::byte{42};
        require(ephemeral.used() == 128, "the shared ledger charges the buffer");
        require(ephemeral.peak() == 128, "charged peak includes live payload");
        {
            const auto lease = ephemeral.reserve(256);
            require(lease.has_value() && ephemeral.used() == 384 && ephemeral.peak() == 384,
                    "native reservations share the payload high-water ledger");
            require(!ephemeral.reserve(129) && ephemeral.peak() == 384,
                    "budget refusal cannot invent a charge");
        }
        require(ephemeral.used() == 128 && ephemeral.peak() == 384,
                "refunding native storage preserves the observed peak");
    }
    require(survivor.bytes().front() == std::byte{42}, "an owning buffer may outlive its budget");
    core::Budget budget{512};
    auto original = image::Plane<std::uint8_t>::allocate(budget, 4, 1).value();
    const auto moved = std::move(original);
    // The move operation explicitly guarantees an empty source. This is that regression.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    require(original.empty() && original.width() == 0 && original.view().empty(),
            "a moved-from plane has an empty shape as well as empty storage");
    std::array<std::uint16_t, 4> codes{0, 32768, 65535, 42};
    const auto input = image::PlaneView<const std::uint16_t>::create(
        codes, {.width = 4, .height = 1, .stride = sizeof(codes)});
    const auto workers = exec::Concurrency::resolve(1, 1, 0, 0);
    const auto method = methods::FixedThreshold::create(0.5);
    require(input && workers && method, "moved-destination binary control has admitted inputs");
    if (!input || !workers || !method) {
        return;
    }
    const exec::Scheduler scheduler{*workers};
    const auto rejected = methods::binarize(*method, {*input, original.view(), budget, scheduler});
    require(!rejected && rejected.error().code == core::ErrorCode::argument,
            "a moved-from destination is rejected before row access");
    survivor = core::Buffer{};
    require(survivor.empty(), "the final owner safely releases the escaped ledger");
}
inline void view_cases() {
    std::array<float, 16> storage{};
    using View = image::PlaneView<float>;
    static_assert(!std::is_constructible_v<View, std::span<float>, image::PlaneShape>);
    require(!View::create(storage, {.width = 0, .height = 1, .stride = 4}),
            "zero width is rejected");
    require(!View::create(storage, {.width = 1, .height = 0, .stride = 4}),
            "zero height is rejected");
    require(!View::create(storage, {.width = 1, .height = 1, .stride = 3}),
            "fractional sample strides are rejected");
    require(!View::create(storage, {.width = 2, .height = 1, .stride = 4}),
            "short strides are rejected");
    require(!View::create(storage, {.width = 4, .height = 5, .stride = 16}),
            "short storage is rejected");
    require(!View::create(storage,
                          {
                              .width = 1,
                              .height = std::numeric_limits<std::uint32_t>::max(),
                              .stride = std::numeric_limits<std::size_t>::max() - 3,
                          }),
            "hostile view extents fail without arithmetic overflow");
    const auto partial =
        View::create(std::span{storage}.first(9), {.width = 1, .height = 3, .stride = 16});
    require(partial.has_value() && partial->row(2).size() == 1,
            "last-row trailing padding need not exist");
    const auto first =
        View::create(std::span{storage}.first(8), {.width = 4, .height = 2, .stride = 16}).value();
    const auto second =
        View::create(std::span{storage}.last(8), {.width = 4, .height = 2, .stride = 16}).value();
    const auto overlapping =
        View::create(std::span{storage}.subspan(1, 8), {.width = 4, .height = 2, .stride = 16})
            .value();
    require(!image::overlaps(first, second), "adjacent disjoint views do not overlap");
    require(image::overlaps(first, overlapping), "partial overlap is found in either direction");
    require(image::overlaps(overlapping, first), "overlap checks are symmetric");
}
inline void threshold_cases() {
    // Exact Q=257*s embedding retains every old normalized sample and threshold endpoint;
    // full-Q16 equality/adjacent-threshold and byte-overlap controls exercise the actual API.
    binary_old_embedding_case();
    binary_fixed_minimal_case();
    binary_input_alias_cases();
    require(!methods::FixedThreshold::create(std::numeric_limits<double>::quiet_NaN()),
            "NaN cannot enter a typed threshold method");
    require(!methods::FixedThreshold::create(std::numeric_limits<double>::infinity()),
            "infinity cannot enter a typed threshold method");
    require(!methods::FixedThreshold::create(-0.1), "negative thresholds fail admission");
}
inline void execution_exception_cases() {
    for (const unsigned workers : {1U, 4U}) {
        const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, workers, 0, 0).value()};
        const auto bad_alloc = [](std::size_t) -> core::Result<void> { throw std::bad_alloc{}; };
        const auto unexpected = [](std::size_t) -> core::Result<void> {
            throw std::runtime_error{"test"};
        };
        require(scheduler.for_each(8, exec::WorkRef{bad_alloc}).error().code ==
                    core::ErrorCode::resource,
                "task allocation failures are values on all execution paths");
        require(scheduler.for_each(8, exec::WorkRef{unexpected}).error().code ==
                    core::ErrorCode::invariant,
                "unexpected task exceptions cannot escape worker threads");
    }
}
inline void admission_cases() {
    static_assert(!std::is_default_constructible_v<app::ProcessRequest>);
    app::Invocation invocation{};
    invocation.command = app::Command::process;
    invocation.operands = {"input.png"};
    invocation.option(app::OptionId::out_dir) = "output";
    invocation.option(app::OptionId::output_mode) = "bw";
    invocation.option(app::OptionId::binarize) = "fixed";
    auto accepted = app::prepare_process(invocation);
    require(accepted.has_value() &&
                std::get<methods::FixedThreshold>(
                    std::get<methods::BinaryOutput>(accepted->plan().output).method)
                        .threshold() == 0.5,
            "application admission owns defaults");
    invocation.option(app::OptionId::fixed_threshold) = "NaN";
    require(!app::prepare_process(invocation),
            "the processing port cannot receive an invalid threshold");
    invocation.option(app::OptionId::fixed_threshold).reset();
    invocation.option(app::OptionId::out_dir) = std::string{"output\0hidden", 13};
    require(!app::prepare_process(invocation), "paths cannot be silently truncated by C APIs");
}
inline void working_method_reference_cases() {
    working_cases();
    gamma_working_cases();
    levels_working_cases();
    clahe_working_cases();
    unsharp_working_cases();
    tvl1_working_cases();
    tvl1_parallel_state_case();
    gaussian_band_cases();
    contrast_fact_cases();
    sharpen_fact_cases();
    binary_fact_cases();
    nlm_cases();
    nlm_working_control_cases();
    tvl1_fact_cases();
    morphology_fact_cases();
    morphology_phase_boundary_cases();
    illumination_receiver_cases();
    morphology_kernel_baseline_cases();
    morphology_absent_mask_case();
    morphology_worker_case();
    morphology_constant_and_excursion_cases();
    morphology_fallback_case();
    morphology_gain_boundary_cases();
    morphology_maximum_worker_case(false);
    morphology_maximum_worker_case(true);
    morphology_interval_cases();
    morphology_negative_zero_case();
    morphology_resource_cases();
    morphology_sample_cap_case();
    morphology_noop_and_cold_cases();
    morphology_phase_cancel_cases();
    morphology_map_healthy_case(false);
    morphology_map_healthy_case(true);
    morphology_map_stop_case(4154, false);
    morphology_map_stop_case(4155, false);
    morphology_map_stop_case(4155, true);
    surface_fact_cases();
    surface_source_cases();
    surface_worker_cases();
    surface_absent_mask_case();
    surface_full_fallback_case();
    surface_dark_reference_case();
    surface_noop_cases();
    surface_automatic_cases();
    surface_gain_cases();
    surface_resource_cases();
    surface_cancel_cases();
    surface_grid_noop_case();
    surface_hierarchy_working_case();
    surface_prefix_case(false);
    surface_prefix_case(true);
    restoration_fact_cases();
    stage_report_cases();
    warning_cases();
}
inline void architecture_cases() {
    ownership_cases();
    statistic_plane_cases();
    view_cases();
    threshold_cases();
    execution_exception_cases();
    admission_cases();
    working_method_reference_cases();
    binary_numerical_cases();
}
} // namespace docenhance::tests
