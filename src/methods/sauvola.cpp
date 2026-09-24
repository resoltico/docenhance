// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/methods/sauvola.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <ranges>
#include <span>

namespace docenhance::methods {
namespace {
constexpr std::uint64_t white = 255;
constexpr std::uint64_t largest_window = Sauvola::max_window;
constexpr std::uint64_t largest_area = largest_window * largest_window;
static_assert(largest_area <=
              std::numeric_limits<std::uint64_t>::max() / largest_area / (white * white));
static_assert(std::numeric_limits<std::uint32_t>::max() <= image::max_reflect_extent());

std::uint32_t strip_count(std::uint32_t width) noexcept {
    return (width / sauvola_strip_columns) +
           static_cast<std::uint32_t>(width % sauvola_strip_columns != 0);
}
std::uint32_t fold(std::int64_t at, std::uint32_t extent) noexcept {
    return static_cast<std::uint32_t>(image::reflect101_folded(at, extent));
}
// Visit at most one real reflected period, with multiplicity for repeated periods. A singleton
// dimension is one sample even for a 4095-wide window; narrow/tall pages do not multiply work by w.
template <typename Add>
bool window_samples(std::uint32_t center, std::uint32_t extent, std::uint32_t window,
                    const Add& add) {
    const std::uint64_t period = extent == 1 ? 1 : std::uint64_t{2} * (extent - 1);
    const auto cycles = window / period;
    const auto remainder = window % period;
    const auto count = std::min<std::uint64_t>(window, period);
    const auto first = static_cast<std::int64_t>(center) - (window / 2);
    for (std::uint64_t i = 0; i < count; ++i) {
        if (!add(fold(first + static_cast<std::int64_t>(i), extent),
                 cycles + static_cast<std::uint64_t>(i < remainder))) {
            return false;
        }
    }
    return true;
}
struct Moments {
    std::uint64_t sum = 0;
    std::uint64_t squares = 0;
    void add(Moments other, std::uint64_t count = 1) noexcept {
        sum += other.sum * count;
        squares += other.squares * count;
    }
    void remove(Moments other) noexcept {
        sum -= other.sum;
        squares -= other.squares;
    }
};
struct Columns {
    std::span<std::uint64_t> sums;
    std::span<std::uint64_t> squares;
    std::uint32_t first{};
    [[nodiscard]] Moments at(std::uint32_t column) const noexcept {
        const auto index = column - first;
        return {
            .sum = sums.subspan(index, 1).front(),
            .squares = squares.subspan(index, 1).front(),
        };
    }
};
struct StripRange {
    std::uint32_t first{};
    std::uint32_t end{};
    core::Cancellation cancellation;
};
struct Strip {
    image::PlaneView<const std::uint8_t> source;
    image::PlaneView<std::uint8_t> destination;
    std::reference_wrapper<const Sauvola> method;
    std::uint32_t first;
    std::uint32_t end;
    Columns columns;
    core::Cancellation cancellation;

    Strip(image::PlaneView<const std::uint8_t> source_view,
          image::PlaneView<std::uint8_t> destination_view, const Sauvola& selected,
          const StripRange& range, Columns workspace_columns)
        : source(source_view), destination(destination_view), method(selected), first(range.first),
          end(range.end), columns(workspace_columns), cancellation(range.cancellation) {}

    [[nodiscard]] bool initialize() const {
        std::ranges::fill(columns.sums, 0);
        std::ranges::fill(columns.squares, 0);
        return window_samples(
            0, source.height(), method.get().window(), [&](std::uint32_t row, std::uint64_t count) {
                if (cancellation.requested(core::Checkpoint::initialization)) {
                    return false;
                }
                const auto samples = source.row(row).subspan(columns.first, columns.sums.size());
                for (auto [p, sum, squares] :
                     std::views::zip(samples, columns.sums, columns.squares)) {
                    const auto value = static_cast<std::uint64_t>(p);
                    sum += value * count;
                    squares += value * value * count;
                }
                return true;
            });
    }
    void advance(std::uint32_t row) const {
        const auto radius = static_cast<std::int64_t>(method.get().window() / 2);
        const auto outgoing =
            source.row(fold(static_cast<std::int64_t>(row) - radius, source.height()))
                .subspan(columns.first, columns.sums.size());
        const auto incoming =
            source.row(fold(static_cast<std::int64_t>(row) + radius + 1, source.height()))
                .subspan(columns.first, columns.sums.size());
        for (auto [old_value, new_value, sum, squares] :
             std::views::zip(outgoing, incoming, columns.sums, columns.squares)) {
            const auto old_sample = static_cast<std::uint64_t>(old_value);
            const auto new_sample = static_cast<std::uint64_t>(new_value);
            sum = sum - old_sample + new_sample;
            squares = squares - (old_sample * old_sample) + (new_sample * new_sample);
        }
    }
    [[nodiscard]] bool write_row(std::uint32_t row) const {
        Moments total;
        const auto initialized = window_samples(first, source.width(), method.get().window(),
                                                [&](std::uint32_t column, std::uint64_t count) {
                                                    total.add(columns.at(column), count);
                                                    return true;
                                                });
        if (!initialized) {
            return false;
        }
        const auto area = static_cast<std::uint64_t>(method.get().window()) * method.get().window();
        const auto n = static_cast<double>(area);
        const auto radius = static_cast<std::int64_t>(method.get().window() / 2);
        const auto in = source.row(row);
        const auto out = destination.row(row);
        for (std::uint32_t x = first; x < end; ++x) {
            constexpr std::uint32_t chunk_samples = 1024;
            if ((x - first) % chunk_samples == 0 &&
                cancellation.requested(core::Checkpoint::processing)) {
                return false;
            }
            // Exact integer cancellation avoids catastrophic floating mean-square subtraction.
            const auto variance_numerator = (area * total.squares) - (total.sum * total.sum);
            const double mean = static_cast<double>(total.sum) / n;
            const double deviation = std::sqrt(static_cast<double>(variance_numerator)) / n;
            const double threshold =
                mean *
                (1.0 + (method.get().k() *
                        ((deviation / (static_cast<double>(white) * method.get().r())) - 1.0)));
            out.subspan(x, 1).front() =
                in.subspan(x, 1).front() <= threshold ? 0 : static_cast<std::uint8_t>(white);
            if (x + 1 < end) {
                total.remove(
                    columns.at(fold(static_cast<std::int64_t>(x) - radius, source.width())));
                total.add(
                    columns.at(fold(static_cast<std::int64_t>(x) + radius + 1, source.width())));
            }
        }
        return true;
    }
    [[nodiscard]] bool run() const {
        if (!initialize()) {
            return false;
        }
        for (std::uint32_t row = 0; row < source.height(); ++row) {
            if (cancellation.requested(core::Checkpoint::processing) || !write_row(row)) {
                return false;
            }
            if (row + 1 < source.height()) {
                advance(row);
            }
        }
        return true;
    }
};
struct Work {
    image::PlaneView<const std::uint8_t> source;
    image::PlaneView<std::uint8_t> destination;
    std::reference_wrapper<const Sauvola> method;
    image::PlaneView<std::uint64_t> workspace;
    unsigned slots{};
    core::Cancellation cancellation;
    core::Result<void> operator()(std::size_t slot) const {
        const auto radius = method.get().window() / 2;
        for (auto tile = slot; tile < strip_count(source.width()); tile += slots) {
            const auto first = static_cast<std::uint32_t>(tile) * sauvola_strip_columns;
            const auto end = first + std::min(sauvola_strip_columns, source.width() - first);
            const auto source_first = first - std::min(radius, first);
            const auto source_end = end + std::min(radius, source.width() - end);
            const auto count = source_end - source_first;
            const Strip strip{
                source,
                destination,
                method.get(),
                {.first = first, .end = end, .cancellation = cancellation},
                {
                    .sums = workspace.row(static_cast<std::uint32_t>(2 * slot)).first(count),
                    .squares =
                        workspace.row(static_cast<std::uint32_t>((2 * slot) + 1)).first(count),
                    .first = source_first,
                },
            };
            if (!strip.run()) {
                return core::cancelled();
            }
        }
        return {};
    }
};
} // namespace
core::Result<SauvolaWorkspace> sauvola_workspace(std::uint32_t width, const Sauvola& method,
                                                 unsigned workers) {
    if (width == 0 || workers < exec::min_workers || workers > exec::max_workers) {
        return core::failure(core::ErrorCode::argument,
                             "B02 workspace requires a width and 1..64 workers");
    }
    const auto columns = std::min(width, sauvola_strip_columns + method.window() - 1);
    const auto slots = std::min(workers, strip_count(width));
    const auto shape = image::plane_shape(columns, 2 * slots, sizeof(std::uint64_t));
    if (!shape) {
        return std::unexpected(shape.error());
    }
    const auto bytes = image::plane_bytes(*shape);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return SauvolaWorkspace{.shape = *shape, .slots = slots, .bytes = *bytes};
}
core::Result<void> sauvola(image::PlaneView<const std::uint8_t> source,
                           image::PlaneView<std::uint8_t> destination, const Sauvola& method,
                           BinarizationContext context) {
    if (source.empty() || destination.empty() || source.width() != destination.width() ||
        source.height() != destination.height() || image::overlaps(source, destination)) {
        return core::failure(core::ErrorCode::argument,
                             "B02 requires nonempty, equally sized, disjoint planes");
    }
    const auto plan = sauvola_workspace(source.width(), method, context.scheduler.get().workers());
    if (!plan) {
        return std::unexpected(plan.error());
    }
    if (context.scheduler.get().cancellation().requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto workspace = image::Plane<std::uint64_t>::allocate(context.budget.get(), plan->shape.width,
                                                           plan->shape.height);
    if (!workspace) {
        return std::unexpected(workspace.error());
    }
    const Work work{
        .source = source,
        .destination = destination,
        .method = method,
        .workspace = workspace->view(),
        .slots = plan->slots,
        .cancellation = context.scheduler.get().cancellation(),
    };
    return context.scheduler.get().for_each(plan->slots, exec::WorkRef{work});
}
} // namespace docenhance::methods
