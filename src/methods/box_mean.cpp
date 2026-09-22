// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/methods/box_mean.hpp"

#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <ranges>
#include <span>

namespace docenhance::methods {
namespace {
// The window, and the reciprocal of its length. Dividing once per pass and multiplying afterwards
// keeps one rounding per output sample.
struct Window {
    std::int64_t radius = 0;
    double scale = 1.0;
};

// Sums accumulate in double: a running sum in float would lose the low bits of a long row, and a
// page is thousands of samples wide.
using ColumnSums = std::array<double, tile_columns>;

// A plane extent is a 32-bit count, so it always folds: the reflection needs no runtime guard.
static_assert(std::numeric_limits<std::uint32_t>::max() <= image::max_reflect_extent());

[[nodiscard]] std::size_t fold(std::int64_t coordinate, std::uint32_t extent) noexcept {
    return image::reflect101_folded(coordinate, extent);
}

[[nodiscard]] std::uint64_t window_length(std::int64_t radius) noexcept {
    return (static_cast<std::uint64_t>(radius) * 2U) + 1U;
}

[[nodiscard]] std::uint64_t reflection_period(std::uint32_t extent) noexcept {
    return static_cast<std::uint64_t>(extent - 1U) * 2U;
}

// A reflected window is periodic. Summing full periods first changes initialization from O(radius)
// to O(extent) when a requested radius reaches beyond the input many times.
template <typename Sample>
[[nodiscard]] double reflected_window_sum(Sample sample, std::int64_t center, std::uint32_t extent,
                                          std::int64_t radius) {
    const auto length = window_length(radius);
    if (extent == 1U) {
        return static_cast<double>(length) * sample(0);
    }
    const auto period = reflection_period(extent);
    const auto begin = center - radius;
    if (length <= period) {
        double sum = 0.0;
        for (std::uint64_t offset = 0; offset < length; ++offset) {
            sum += sample(static_cast<std::int64_t>(offset) + begin);
        }
        return sum;
    }
    double period_sum = 0.0;
    for (std::uint32_t index = 0; index < extent; ++index) {
        const double multiplicity = index == 0U || index + 1U == extent ? 1.0 : 2.0;
        period_sum += multiplicity * sample(static_cast<std::int64_t>(index));
    }
    const auto complete_periods = length / period;
    const auto full_periods = static_cast<double>(complete_periods);
    double sum = full_periods * period_sum;
    for (std::uint64_t offset = 0; offset < length % period; ++offset) {
        sum += sample(static_cast<std::int64_t>(offset) + begin);
    }
    return sum;
}

// One row of the horizontal pass: the window slides by adding the sample that enters it and
// subtracting the one that leaves, which is why the cost does not grow with the radius.
void mean_along_row(std::span<const float> source, std::span<float> destination, Window window) {
    const auto width = static_cast<std::uint32_t>(source.size());
    // The one place a sample is reached by a computed index; every index is folded into the plane
    // first, and the sanitizer presets check that with a hardened standard library.
    const auto sample = [source, width](std::int64_t at) {
        return static_cast<double>(source[fold(at, width)]); // NOLINT(*-unchecked-container-access)
    };
    double sum = reflected_window_sum(sample, 0, width, window.radius);
    std::uint32_t x = 0;
    for (float& mean : destination) {
        mean = static_cast<float>(sum * window.scale);
        if (x + 1U < width) {
            sum += sample(static_cast<std::int64_t>(x) + window.radius + 1) -
                   sample(static_cast<std::int64_t>(x) - window.radius);
        }
        ++x;
    }
}

// One tile of the vertical pass: the same sliding window, held for a block of columns at once so
// that every read is a whole row and the sums stay in cache.
void mean_down_tile(image::PlaneView<const float> source, image::PlaneView<float> destination,
                    Window window, std::uint32_t first_column, std::uint32_t first_row) {
    const auto height = source.height();
    const auto columns = std::min(tile_columns, source.width() - first_column);
    const auto last_row = std::min(first_row + tile_rows, height);
    ColumnSums sums{};
    const auto block = std::span{sums}.first(columns);
    const auto accumulate = [&](std::int64_t row, double sign) {
        const auto samples = source.row(static_cast<std::uint32_t>(fold(row, height)));
        for (auto [sum, value] : std::views::zip(block, samples.subspan(first_column, columns))) {
            sum += sign * static_cast<double>(value);
        }
    };
    // Every tile rebuilds its own window, which keeps tiles independent of worker count. A large
    // radius uses one reflected period per column instead of iterating the radius itself.
    if (window_length(window.radius) <= reflection_period(height)) {
        for (std::int64_t offset = -window.radius; offset <= window.radius; ++offset) {
            accumulate(static_cast<std::int64_t>(first_row) + offset, 1.0);
        }
    } else {
        auto sum = block.begin();
        for (std::uint32_t offset = 0; offset < columns; ++offset, ++sum) {
            const auto column = first_column + offset;
            *sum = reflected_window_sum(
                [&](std::int64_t row) {
                    const auto samples = source.row(static_cast<std::uint32_t>(fold(row, height)));
                    return static_cast<double>(samples.subspan(column, 1).front());
                },
                static_cast<std::int64_t>(first_row), height, window.radius);
        }
    }
    for (std::uint32_t y = first_row; y < last_row; ++y) {
        const auto out = destination.row(y).subspan(first_column, columns);
        for (auto [mean, sum] : std::views::zip(out, block)) {
            mean = static_cast<float>(sum * window.scale);
        }
        accumulate(static_cast<std::int64_t>(y) + window.radius + 1, 1.0);
        accumulate(static_cast<std::int64_t>(y) - window.radius, -1.0);
    }
}

[[nodiscard]] std::uint32_t tiles_of(std::uint32_t extent, std::uint32_t tile) noexcept {
    return (extent / tile) + static_cast<std::uint32_t>(extent % tile != 0U);
}

// Two planes share memory when their byte ranges intersect. Compared as addresses rather than as
// pointers, so the comparison itself stays inside the rules.
[[nodiscard]] bool overlapping(std::span<const float> left, std::span<const float> right) noexcept {
    const auto left_begin = std::bit_cast<std::uintptr_t>(left.data());
    const auto right_begin = std::bit_cast<std::uintptr_t>(right.data());
    return left_begin <= right_begin ? right_begin - left_begin < left.size_bytes()
                                     : left_begin - right_begin < right.size_bytes();
}

[[nodiscard]] core::Result<void> usable(image::PlaneView<const float> source,
                                        image::PlaneView<float> destination, std::uint32_t radius) {
    if (source.empty() || source.width() != destination.width() ||
        source.height() != destination.height()) {
        return core::failure(core::ErrorCode::argument,
                             "A box mean needs a non-empty source and a destination of the same "
                             "size");
    }
    if (radius == 0) {
        return core::failure(core::ErrorCode::argument,
                             "A box mean needs a radius of at least one sample");
    }
    if (overlapping(source.storage(), destination.storage())) {
        return core::failure(core::ErrorCode::argument,
                             "A box mean reads its source after writing its destination, so the "
                             "two must be different planes");
    }
    return {};
}
} // namespace

core::Result<void> box_mean(image::PlaneView<const float> source,
                            image::PlaneView<float> destination, std::uint32_t radius,
                            const exec::Scheduler& scheduler, core::Budget& budget) {
    if (auto checked = usable(source, destination, radius); !checked) {
        return checked;
    }
    auto intermediate = image::Plane<float>::allocate(budget, source.width(), source.height());
    if (!intermediate) {
        return std::unexpected(intermediate.error());
    }
    const Window window{
        .radius = radius,
        .scale = 1.0 / ((2.0 * static_cast<double>(radius)) + 1.0),
    };
    const auto rows = intermediate->view();
    const auto row_tiles = tiles_of(source.height(), tile_rows);
    const auto across =
        scheduler.for_each(row_tiles, exec::WorkRef{[&](std::size_t tile) {
                               const auto first = static_cast<std::uint32_t>(tile) * tile_rows;
                               const auto last = std::min(first + tile_rows, source.height());
                               for (std::uint32_t y = first; y < last; ++y) {
                                   mean_along_row(source.row(y), rows.row(y), window);
                               }
                               return core::Result<void>{};
                           }});
    if (!across) {
        return across;
    }
    const auto column_tiles = tiles_of(source.width(), tile_columns);
    const auto constant = rows.as_const();
    return scheduler.for_each(
        static_cast<std::size_t>(column_tiles) * row_tiles, exec::WorkRef{[&](std::size_t tile) {
            const auto column = static_cast<std::uint32_t>(tile % column_tiles) * tile_columns;
            const auto row = static_cast<std::uint32_t>(tile / column_tiles) * tile_rows;
            mean_down_tile(constant, destination, window, column, row);
            return core::Result<void>{};
        }});
}
} // namespace docenhance::methods
