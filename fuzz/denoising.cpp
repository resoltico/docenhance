// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/denoising.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/opencv/nlm.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
namespace {
void check(std::span<const std::uint8_t> bytes) {
    namespace core = docenhance::core;
    namespace image = docenhance::image;
    namespace methods = docenhance::methods;
    namespace native = docenhance::opencv;
    using docenhance::fuzz::require;
    docenhance::fuzz::FuzzInput input{bytes};
    constexpr std::uint32_t size_choices = 8;
    const auto side = methods::nlm_min_search + (input.byte() % size_choices);
    const auto method =
        methods::Nlm::create({.patch = methods::nlm_min_patch, .search = methods::nlm_min_search})
            .value();
    constexpr std::size_t limit = std::size_t{8} * 1024 * 1024;
    core::Budget budget{limit};
    auto source = image::Plane<std::uint16_t>::allocate(budget, side, side).value();
    auto destination = image::Plane<std::uint16_t>::allocate(budget, side, side).value();
    auto reference = image::Plane<std::uint16_t>::allocate(budget, side, side).value();
    for (std::uint32_t y = 0; y < side; ++y) {
        const auto row = source.view().row(y);
        for (auto& sample : row) {
            sample = input.integer<std::uint16_t>();
        }
    }
    const auto live = budget.used();
    methods::DenoisingReport report;
    const core::Cancellation cancellation;
    require(
        native::denoise(
            source.view().as_const(), destination.view(),
            {.method = method, .budget = budget, .cancellation = cancellation, .report = report})
            .has_value(),
        "native denoising succeeds within its admitted bound");
    require(
        native::native_tile(source.view().as_const(), reference.view(), method, budget).has_value(),
        "native denoising succeeds within its admitted bound");
    for (std::uint32_t y = 0; y < side; ++y) {
        require(std::ranges::equal(destination.view().row(y), reference.view().row(y)),
                "global tile halo matches native output");
    }
    require(budget.used() == live, "native scratch refunds");
    const auto q = input.integer<std::uint16_t>();
    constexpr double sample = 0.123456789;
    const image::Rgb rgb{sample, sample, sample};
    require(methods::nlm_correct(rgb, q, q, methods::denoising_default_blend).value() == rgb,
            "zero correction preserves entering doubles");
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    check({data, size});
    return 0;
}
