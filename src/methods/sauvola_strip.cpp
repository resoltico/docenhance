// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/parameter_domains.hpp"
#include "sauvola_working_detail.hpp"
#include "wide_unsigned.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <ranges>
#include <span>
namespace docenhance::methods {
namespace {
constexpr std::uint64_t white = 65535;
constexpr std::uint64_t largest_window = sauvola_window_domain.maximum;
static_assert(largest_window * largest_window <= UINT64_MAX / (white * white));
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
struct StripRange {
    std::uint32_t first{};
    std::uint32_t end{};
    core::Cancellation cancellation;
};
struct Strip {
    image::PlaneView<const std::uint16_t> source;
    image::PlaneView<std::uint8_t> destination;
    std::reference_wrapper<const Sauvola> method;
    std::uint32_t first;
    std::uint32_t end;
    BinaryColumns columns;
    core::Cancellation cancellation;
    ClassificationFacts counts;
    bool arithmetic_valid = true;

    Strip(image::PlaneView<const std::uint16_t> source_view,
          image::PlaneView<std::uint8_t> destination_view, const Sauvola& selected,
          const StripRange& range, BinaryColumns workspace_columns)
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
    [[nodiscard]] bool write_row(std::uint32_t row) {
        BinaryMoments total;
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
            const auto variance_numerator = wide_subtract(wide_multiply(area, total.squares),
                                                          wide_multiply(total.sum, total.sum));
            if (!variance_numerator) {
                arithmetic_valid = false;
                return false;
            }
            const double mean = static_cast<double>(total.sum) / n;
            const double deviation = std::sqrt(wide_double(*variance_numerator)) / n;
            const double threshold =
                mean *
                (1.0 + (method.get().k() *
                        ((deviation / (static_cast<double>(white) * method.get().r())) - 1.0)));
            const bool black = static_cast<double>(in.subspan(x, 1).front()) <= threshold;
            out.subspan(x, 1).front() = black ? std::uint8_t{0} : std::uint8_t{1};
            counts.black_samples += static_cast<std::uint64_t>(black);
            counts.white_samples += static_cast<std::uint64_t>(!black);
            if (x + 1 < end) {
                total.remove(
                    columns.at(fold(static_cast<std::int64_t>(x) - radius, source.width())));
                total.add(
                    columns.at(fold(static_cast<std::int64_t>(x) + radius + 1, source.width())));
            }
        }
        return true;
    }
    [[nodiscard]] bool run() {
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
} // namespace
core::Result<ClassificationFacts> binary_sauvola_strip(const BinarySauvolaStrip& context) {
    Strip strip{context.input.codes,
                context.input.out,
                context.method.get(),
                {
                    .first = context.first,
                    .end = context.end,
                    .cancellation = context.input.scheduler.get().cancellation(),
                },
                context.columns};
    if (!strip.run()) {
        return strip.arithmetic_valid ? core::Result<ClassificationFacts>{core::cancelled()}
                                      : core::Result<ClassificationFacts>{core::failure(
                                            core::ErrorCode::invariant, "Invalid binary moments")};
    }
    return strip.counts;
}
} // namespace docenhance::methods
