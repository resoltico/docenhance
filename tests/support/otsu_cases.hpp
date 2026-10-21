// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "otsu_reference.hpp"
#include "require.hpp"

#include <array>
#include <cstdint>
namespace docenhance::tests {
inline void otsu_cases() {
    constexpr std::uint32_t byte_values = 256;
    core::Budget budget{65536};
    auto source = image::Plane<std::uint16_t>::allocate(budget, byte_values, 1);
    auto output = image::Plane<std::uint8_t>::allocate(budget, byte_values, 1);
    require(source && output, "concrete reference source/output allocations succeed");
    std::array<std::uint8_t, byte_values> bytes{};
    for (std::uint32_t x = 0; x < byte_values; ++x) {
        source->view().row(0).subspan(x, 1).front() = static_cast<std::uint16_t>(257 * x);
        bytes.at(x) = static_cast<std::uint8_t>(x);
    }
    const auto held = budget.used();
    const auto byte_view = image::PlaneView<const std::uint8_t>::create(
        bytes, {.width = byte_values, .height = 1, .stride = byte_values});
    require(byte_view.has_value(), "independent byte recipe view is admitted");
    const auto expected = otsu_reference(*byte_view);
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    require(concurrency.has_value(), "concrete reference scheduler is admitted");
    const exec::Scheduler scheduler{*concurrency};
    const auto observation = methods::binarize(
        methods::Otsu::create(), {source->view().as_const(), output->view(), budget, scheduler});
    require(observation.has_value(), "actual Q257 embedding fits and classifies global Otsu");
    require(budget.used() == held, "histogram and classification partials refund their charge");
    require(observation->threshold_bin == expected.threshold_bin &&
                observation->single_bin_fallback == expected.single_bin_fallback,
            "all byte recipe values match independent population oracle");
    for (std::uint32_t x = 0; x < byte_values; ++x) {
        const auto bin = otsu_reference_bin(static_cast<std::uint8_t>(x));
        require(output->view().row(0).subspan(x, 1).front() ==
                    (bin <= observation->threshold_bin ? 0 : 1),
                "all byte values retain exact black and white endpoints");
    }
}
} // namespace docenhance::tests
