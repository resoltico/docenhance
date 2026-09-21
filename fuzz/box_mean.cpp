// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
// Property fuzzing of the box-mean kernel against two independent references: the definition
// itself — every window summed directly, one output sample at a time — and the same kernel run on
// one worker. A crash-only harness would miss both of the things that can actually go wrong here,
// a window that drifts at the borders and a schedule whose answers depend on the worker count.
#include "docenhance/methods/box_mean.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
namespace {
using docenhance::fuzz::FuzzInput;
using docenhance::fuzz::require;
namespace core = docenhance::core;
namespace exec = docenhance::exec;
namespace image = docenhance::image;
namespace methods = docenhance::methods;

// Small planes, because the reference costs the window squared for every sample, and radii that
// reach well past the borders so the reflection folds more than once.
constexpr std::uint32_t max_extent = 24;
constexpr std::uint32_t max_radius = 40;
constexpr unsigned max_workers = 4;
constexpr std::size_t budget_bytes = std::size_t{4} * 1024 * 1024;
constexpr double tolerance = 1e-6;

struct Pixel {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

double naive_mean(const image::PlaneView<const float>& source, Pixel pixel, std::int64_t radius) {
    double sum = 0.0;
    for (std::int64_t dy = -radius; dy <= radius; ++dy) {
        const auto row = source.row(static_cast<std::uint32_t>(
            image::reflect101_folded(static_cast<std::int64_t>(pixel.y) + dy, source.height())));
        for (std::int64_t dx = -radius; dx <= radius; ++dx) {
            sum += static_cast<double>(row[image::reflect101_folded(
                static_cast<std::int64_t>(pixel.x) + dx, source.width())]);
        }
    }
    const auto length = (2.0 * static_cast<double>(radius)) + 1.0;
    return sum / (length * length);
}

void check(FuzzInput& input) {
    const auto width = static_cast<std::uint32_t>(input.bounded(max_extent - 1) + 1);
    const auto height = static_cast<std::uint32_t>(input.bounded(max_extent - 1) + 1);
    const auto radius = static_cast<std::uint32_t>(input.bounded(max_radius - 1) + 1);
    const auto workers = static_cast<unsigned>(input.bounded(max_workers - 1) + 1);
    core::Budget budget{budget_bytes};
    auto page = image::Plane<float>::allocate(budget, width, height);
    auto sequential = image::Plane<float>::allocate(budget, width, height);
    auto parallel = image::Plane<float>::allocate(budget, width, height);
    require(page.has_value() && sequential.has_value() && parallel.has_value(),
            "a small plane always fits this budget");
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto row = page->view().row(y);
        for (float& sample : row) {
            // Finite samples only: the kernel documents that as the decoder's job.
            sample = static_cast<float>(input.unit());
        }
    }
    const auto source = page->view().as_const();
    const auto one = exec::Concurrency::resolve(1, 1, 0, 0);
    const auto many = exec::Concurrency::resolve(workers, workers, 0, 0);
    require(one.has_value() && many.has_value(), "a worker count inside the contract resolves");
    require(methods::box_mean(source, sequential->view(), radius, exec::Scheduler{*one}, budget)
                .has_value(),
            "a valid box mean on one worker succeeds");
    require(methods::box_mean(source, parallel->view(), radius, exec::Scheduler{*many}, budget)
                .has_value(),
            "a valid box mean on many workers succeeds");
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto slow = sequential->view().row(y);
        const auto fast = parallel->view().row(y);
        for (std::uint32_t x = 0; x < width; ++x) {
            require(std::bit_cast<std::uint32_t>(slow[x]) == std::bit_cast<std::uint32_t>(fast[x]),
                    "the worker count never changes a single bit of the result");
            const auto expected = naive_mean(source, {.x = x, .y = y}, radius);
            require(std::abs(static_cast<double>(slow[x]) - expected) < tolerance,
                    "the sliding window agrees with the summed definition");
        }
    }
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    FuzzInput input(std::span<const std::uint8_t>(data, size));
    check(input);
    return 0;
}
