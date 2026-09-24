// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/methods/sauvola.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "sauvola_reference.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace {
constexpr std::size_t budget_limit = 65536;
constexpr std::uint32_t max_extent = 13;
constexpr std::uint32_t window_variants = 8;
constexpr std::uint32_t min_window = 3;
constexpr double byte_scale = 255.0;
std::uint8_t byte_at(std::span<const std::uint8_t> bytes, std::size_t index) {
    return index < bytes.size() ? bytes.subspan(index, 1).front() : 0;
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using docenhance::fuzz::require;
    namespace core = docenhance::core;
    namespace exec = docenhance::exec;
    namespace image = docenhance::image;
    namespace methods = docenhance::methods;
    namespace tests = docenhance::tests;
    const std::span<const std::uint8_t> bytes{data, size};
    const auto width = 1U + (byte_at(bytes, 0) % max_extent);
    const auto height = 1U + (byte_at(bytes, 1) % max_extent);
    const auto window = min_window + (2U * (byte_at(bytes, 2) % window_variants));
    const double k = byte_at(bytes, 3) / byte_scale;
    const double r = (1.0 + byte_at(bytes, 4)) / (byte_scale + 1.0);
    // Clamp to the declared one-quantum floor, never a value outside the typed domain.
    const auto method =
        methods::Sauvola::create({
                                     .window = window,
                                     .k = k,
                                     .r = r < methods::Sauvola::min_r ? methods::Sauvola::min_r : r,
                                 })
            .value();
    core::Budget budget{budget_limit};
    auto source = image::Plane<std::uint8_t>::allocate(budget, width, height).value();
    auto output = image::Plane<std::uint8_t>::allocate(budget, width, height).value();
    constexpr std::size_t header_bytes = 5;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            source.view().row(y).subspan(x, 1).front() =
                byte_at(bytes, header_bytes + (static_cast<std::size_t>(y) * width) + x);
        }
    }
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const auto held = budget.used();
    require(methods::sauvola(source.view().as_const(), output.view(), method,
                             {.scheduler = scheduler, .budget = budget})
                .has_value(),
            "valid B02 kernel succeeds");
    require(budget.used() == held, "scratch is fully refunded");
    const tests::SauvolaReference reference{
        .source = source.view().as_const(),
        .window = window,
        .k = method.k(),
        .r = method.r(),
    };
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            require(output.view().row(y).subspan(x, 1).front() == reference.at(x, y),
                    "rolling moments match the direct window oracle");
        }
    }
    return 0;
}
