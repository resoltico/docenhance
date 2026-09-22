// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/box_mean.hpp"
#include "require.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <vector>
namespace docenhance::tests {
inline void memory_cases() {
    constexpr std::size_t mebibyte = std::size_t{1024} * 1024;
    core::Budget budget{4 * mebibyte};
    require(budget.used() == 0, "a new budget has spent nothing");
    {
        auto buffer = budget.allocate(mebibyte);
        require(buffer.has_value(), "an allocation inside the budget succeeds");
        require(buffer->size() == mebibyte, "a buffer is the size it was asked for");
        require(budget.used() == mebibyte, "an allocation is charged");
        const auto address = std::bit_cast<std::uintptr_t>(buffer->bytes().data());
        require(address % core::buffer_alignment == 0, "buffers start on the alignment");
        const auto moved = std::move(*buffer);
        require(budget.used() == mebibyte, "a move does not change what is charged");
        require(moved.size() == mebibyte, "a move keeps the memory");
    }
    require(budget.used() == 0, "a destroyed buffer refunds its charge");
    require(budget.allocate(budget.limit() + 1).error().code == core::ErrorCode::resource,
            "exceeding the budget is a resource failure, not an exception");
    require(budget.used() == 0, "a refused allocation is not charged");
    const auto empty = budget.allocate(0);
    require(empty.has_value() && empty->empty(), "zero bytes is an empty buffer");

    // A page-sized plane: the shape is checked before anything is allocated.
    const auto shape = image::plane_shape(1000, 700, sizeof(float));
    require(shape.has_value(), "an ordinary page has a shape");
    require(shape->stride % core::buffer_alignment == 0, "every row starts aligned");
    require(shape->stride >= 1000 * sizeof(float), "a row holds its samples");
    require(image::plane_bytes(*shape).value() == shape->stride * 700, "a plane is its rows");
    require(!image::plane_shape(0, 700, sizeof(float)).has_value(), "an empty plane is refused");
    const auto huge = std::numeric_limits<std::uint32_t>::max();
    require(image::plane_shape(huge, huge, sizeof(float)).has_value(),
            "a huge shape is describable");
    require(!image::plane_bytes(image::plane_shape(huge, huge, sizeof(float)).value()).has_value(),
            "a plane that cannot be addressed is refused instead of overflowing");

    auto plane = image::Plane<float>::allocate(budget, 64, 8);
    require(plane.has_value(), "a small plane fits the budget");
    require(plane->width() == 64 && plane->height() == 8, "a plane keeps its size");
    require(budget.used() == plane->size_bytes(), "a plane is charged to the budget");
    const auto view = plane->view();
    require(view.row(0).size() == 64, "a row is as wide as the plane");
    const auto first = std::bit_cast<std::uintptr_t>(view.row(0).data());
    const auto second = std::bit_cast<std::uintptr_t>(view.row(1).data());
    require(second - first == plane->shape().stride, "rows are one stride apart");
    require(second % core::buffer_alignment == 0, "every row is aligned, not only the first");
    require(view.as_const().row(0).size() == 64, "a mutable view can be read-only");
    require(!image::Plane<float>::allocate(budget, huge, huge).has_value(),
            "a plane larger than the budget is refused as a value");
}
inline void schedule_cases() {
    constexpr std::size_t mebibyte = std::size_t{1024} * 1024;
    // `auto` follows the contract: min(4, max(1, hardware)), never the size of the machine.
    const auto automatic = exec::Concurrency::resolve(std::nullopt, 32, 0, 0);
    require(automatic->workers() == exec::automatic_workers, "auto is modest on a large machine");
    require(exec::Concurrency::resolve(std::nullopt, 2, 0, 0)->workers() == 2,
            "auto never exceeds the machine");
    require(exec::Concurrency::resolve(std::nullopt, 0, 0, 0)->workers() == 1,
            "an unreported machine still runs");
    require(exec::Concurrency::resolve(8, 2, 0, 0)->workers() == 8,
            "an explicit count is honoured");
    require(exec::Concurrency::resolve(0, 8, 0, 0).error().code == core::ErrorCode::argument,
            "zero workers is an argument error");
    require(exec::Concurrency::resolve(exec::max_workers + 1, 8, 0, 0).error().code ==
                core::ErrorCode::argument,
            "more than the contract allows is an argument error");
    // The budget has the last word: workers are reduced to what their working sets fit into.
    require(exec::Concurrency::resolve(16, 16, 4 * mebibyte, mebibyte)->workers() == 4,
            "the budget caps the worker count");
    require(exec::Concurrency::resolve(std::nullopt, 16, mebibyte, 4 * mebibyte).error().code ==
                core::ErrorCode::resource,
            "a budget too small for one worker is refused, not silently shrunk");

    // The same answers, whatever the worker count: each item owns one slot and nothing else.
    constexpr std::size_t items = 500;
    for (const unsigned workers : {1U, 2U, 7U}) {
        std::vector<std::size_t> squares(items, 0);
        const exec::Scheduler scheduler{*exec::Concurrency::resolve(workers, workers, 0, 0)};
        const auto run = scheduler.for_each(items, exec::WorkRef{[&squares](std::size_t index) {
                                                squares.at(index) = index * index;
                                                return core::Result<void>{};
                                            }});
        require(run.has_value(), "an independent run succeeds");
        require(squares.at(items - 1) == (items - 1) * (items - 1), "every item ran");
        require(std::ranges::count(squares, 0) == 1, "every item ran exactly once");
    }
    // The failure reported is the one a sequential run would have reported.
    const exec::Scheduler parallel{*exec::Concurrency::resolve(8, 8, 0, 0)};
    const auto failed =
        parallel.for_each(items, exec::WorkRef{[](std::size_t index) {
                              return index % 100 == 0 && index > 0
                                         ? core::Result<void>{core::failure(
                                               core::ErrorCode::invariant, std::to_string(index))}
                                         : core::Result<void>{};
                          }});
    require(!failed.has_value() && failed.error().message == "100",
            "the lowest failing index is the one reported");
    require(parallel
                .for_each(0, exec::WorkRef{[](std::size_t) {
                              return core::Result<void>{
                                  core::failure(core::ErrorCode::invariant, "x")};
                          }})
                .has_value(),
            "no items is not a failure");
}
// A deterministic stand-in for a page: no randomness, no fixtures, the same samples everywhere.
inline float probe_sample(std::uint32_t x, std::uint32_t y) {
    const auto mixed = ((x * 37U) ^ (y * 101U)) % 251U;
    return static_cast<float>(mixed) / 251.0F;
}

// What the kernel must agree with: the window, summed directly, one output sample at a time.
struct Pixel {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

inline double naive_box_mean(const image::PlaneView<const float>& source, Pixel pixel,
                             std::int64_t radius) {
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

inline core::Result<image::Plane<float>> filled_plane(core::Budget& budget, std::uint32_t width,
                                                      std::uint32_t height, float constant) {
    auto plane = image::Plane<float>::allocate(budget, width, height);
    if (!plane) {
        return plane;
    }
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto row = plane->view().row(y);
        for (std::uint32_t x = 0; x < width; ++x) {
            row[x] = constant < 0.0F ? probe_sample(x, y) : constant;
        }
    }
    return plane;
}

inline constexpr std::size_t test_mebibyte = std::size_t{1024} * 1024;
inline constexpr std::uint32_t probe_width = 147;
inline constexpr std::uint32_t probe_height = 83;

inline exec::Scheduler scheduler_of(unsigned workers) {
    return exec::Scheduler{*exec::Concurrency::resolve(workers, workers, 0, 0)};
}

inline void box_mean_border_cases() {
    constexpr std::uint32_t width = probe_width;
    constexpr std::uint32_t height = probe_height;
    core::Budget budget{64 * test_mebibyte};
    const auto one = scheduler_of(1);

    // A flat page stays flat, which is the whole border convention in one assertion: a reflected
    // window that lost or repeated samples would dip at the edges.
    auto flat = filled_plane(budget, width, height, 0.25F);
    auto flat_out = image::Plane<float>::allocate(budget, width, height);
    require(
        methods::box_mean(flat->view().as_const(), flat_out->view(), 5, one, budget).has_value(),
        "a flat page blurs");
    for (const std::uint32_t y : {0U, height / 2, height - 1}) {
        const auto row = flat_out->view().row(y);
        for (const std::uint32_t x : {0U, width / 2, width - 1}) {
            require(std::abs(row[x] - 0.25F) < 1e-6F, "a flat page stays flat, edges included");
        }
    }

    // Periodic reflected initialization must not iterate a huge requested radius.
    auto singleton = filled_plane(budget, 1, 1, 0.25F);
    auto singleton_out = image::Plane<float>::allocate(budget, 1, 1);
    require(methods::box_mean(singleton->view().as_const(), singleton_out->view(),
                              std::numeric_limits<std::uint32_t>::max(), one, budget)
                .has_value(),
            "a maximal radius on a singleton finishes");
    require(singleton_out->view().row(0).front() == 0.25F, "a singleton remains unchanged");
}

// Every sample of a plane, against the definition. The reference costs the window squared per
// sample, so a window wider than the plane is checked on a plane small enough to afford it.
inline void agrees_with_definition(std::uint32_t width, std::uint32_t height,
                                   std::uint32_t radius) {
    core::Budget budget{64 * test_mebibyte};
    const auto one = scheduler_of(1);
    auto page = filled_plane(budget, width, height, -1.0F);
    auto blurred = image::Plane<float>::allocate(budget, width, height);
    const auto source = page->view().as_const();
    require(methods::box_mean(source, blurred->view(), radius, one, budget).has_value(),
            "a page blurs at every radius");
    double worst = 0.0;
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto row = blurred->view().row(y);
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto expected = naive_box_mean(source, {.x = x, .y = y}, radius);
            worst = std::max(worst, std::abs(static_cast<double>(row[x]) - expected));
        }
    }
    require(worst < 1e-6, "the sliding window agrees with the definition");
}

inline void box_mean_definition_cases() {
    agrees_with_definition(probe_width, probe_height, 1);
    agrees_with_definition(probe_width, probe_height, 4);
    // A window far wider than the plane folds through the reflection many times over.
    agrees_with_definition(9, 7, 3);
    agrees_with_definition(9, 7, 200);
    // A plane narrower than one tile, and one wider than several.
    agrees_with_definition(1, 5, 2);
    agrees_with_definition(200, 3, 2);
}

inline void box_mean_determinism_cases() {
    constexpr std::uint32_t width = probe_width;
    constexpr std::uint32_t height = probe_height;
    core::Budget budget{64 * test_mebibyte};
    const auto one = scheduler_of(1);
    auto page = filled_plane(budget, width, height, -1.0F);
    const auto source = page->view().as_const();
    // The promise the schedule exists to keep: the same bits, whatever --threads said.
    auto reference = image::Plane<float>::allocate(budget, width, height);
    require(methods::box_mean(source, reference->view(), 7, one, budget).has_value(), "one worker");
    for (const unsigned workers : {2U, 3U, 8U}) {
        auto parallel = image::Plane<float>::allocate(budget, width, height);
        const auto many = scheduler_of(workers);
        require(methods::box_mean(source, parallel->view(), 7, many, budget).has_value(),
                "many workers");
        for (std::uint32_t y = 0; y < height; ++y) {
            const auto expected = reference->view().row(y);
            const auto actual = parallel->view().row(y);
            for (std::uint32_t x = 0; x < width; ++x) {
                require(std::bit_cast<std::uint32_t>(expected[x]) ==
                            std::bit_cast<std::uint32_t>(actual[x]),
                        "more workers never change a single bit of the result");
            }
        }
    }
}

inline void box_mean_refusal_cases() {
    constexpr std::uint32_t width = probe_width;
    constexpr std::uint32_t height = probe_height;
    core::Budget budget{64 * test_mebibyte};
    const auto one = scheduler_of(1);
    auto page = filled_plane(budget, width, height, -1.0F);
    auto blurred = image::Plane<float>::allocate(budget, width, height);
    const auto source = page->view().as_const();
    // What it refuses, as values.
    require(methods::box_mean(source, blurred->view(), 0, one, budget).error().code ==
                core::ErrorCode::argument,
            "a radius of zero is not a filter");
    auto small = image::Plane<float>::allocate(budget, width, height - 1);
    require(methods::box_mean(source, small->view(), 3, one, budget).error().code ==
                core::ErrorCode::argument,
            "a destination of another size is refused");
    require(methods::box_mean(source, page->view(), 3, one, budget).error().code ==
                core::ErrorCode::argument,
            "blurring a plane onto itself is refused");
    std::array<float, 12> shared{};
    const image::PlaneShape shape{.width = 4, .height = 2, .stride = 4 * sizeof(float)};
    const auto offset_source =
        image::PlaneView<const float>::create(std::span<const float>(shared).first(8), shape)
            .value();
    const auto offset_destination =
        image::PlaneView<float>::create(std::span<float>(shared).subspan(1, 8), shape).value();
    require(methods::box_mean(offset_source, offset_destination, 3, one, budget).error().code ==
                core::ErrorCode::argument,
            "partially overlapping views are refused");
    core::Budget tiny{1024};
    require(methods::box_mean(source, blurred->view(), 3, one, tiny).error().code ==
                core::ErrorCode::resource,
            "a budget without room for the intermediate plane is a resource failure");
}

inline void box_mean_shape_cases() {
    constexpr std::uint32_t width = probe_width;
    constexpr std::uint32_t height = probe_height;
    core::Budget budget{64 * test_mebibyte};
    const auto one = scheduler_of(1);
    // A single row and a single column: the degenerate shapes a page can still have.
    auto line = filled_plane(budget, width, 1, -1.0F);
    auto line_out = image::Plane<float>::allocate(budget, width, 1);
    require(
        methods::box_mean(line->view().as_const(), line_out->view(), 3, one, budget).has_value(),
        "a one-row plane blurs");
    auto column = filled_plane(budget, 1, height, -1.0F);
    auto column_out = image::Plane<float>::allocate(budget, 1, height);
    require(methods::box_mean(column->view().as_const(), column_out->view(), 3, one, budget)
                .has_value(),
            "a one-column plane blurs");
}

inline void box_mean_cases() {
    box_mean_border_cases();
    box_mean_definition_cases();
    box_mean_determinism_cases();
    box_mean_refusal_cases();
    box_mean_shape_cases();
}
} // namespace docenhance::tests
