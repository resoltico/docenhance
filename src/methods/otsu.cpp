// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/otsu.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>

namespace docenhance::methods {
namespace {
constexpr std::uint32_t white = std::numeric_limits<std::uint8_t>::max();
constexpr double relative_score_tolerance = 1e-12;
constexpr std::size_t chunk = 1024;
std::uint32_t quantize(std::uint8_t sample) noexcept {
    // No half-integer occurs with the odd denominator; integer rounding is exact.
    return (((otsu_histogram_bins - 1) * sample) + (white / 2)) / white;
}
bool valid_planes(image::PlaneView<const std::uint8_t> source,
                  image::PlaneView<std::uint8_t> destination) noexcept {
    return !source.empty() && !destination.empty() && source.width() == destination.width() &&
           source.height() == destination.height() && !image::overlaps(source, destination);
}
struct Histogram {
    std::span<const std::uint64_t> counts;
    std::uint64_t total_count;
    double total_sum;
    core::Cancellation cancellation;

    template <typename Observe>
    [[nodiscard]] core::Result<void> scores(const Observe& observe) const {
        std::uint64_t lower_count = 0;
        double lower_sum = 0;
        const auto total = static_cast<double>(total_count);
        for (std::uint32_t t = 0; t + 1 < otsu_histogram_bins; ++t) {
            if (t % chunk == 0 && cancellation.requested(core::Checkpoint::solving)) {
                return core::cancelled();
            }
            const auto count = counts.subspan(t, 1).front();
            lower_count += count;
            lower_sum += static_cast<double>(count) * t;
            const auto upper_count = total_count - lower_count;
            if (lower_count == 0 || upper_count == 0) {
                continue;
            }
            const auto lower = static_cast<double>(lower_count);
            const auto upper = static_cast<double>(upper_count);
            const double difference = (lower_sum / lower) - ((total_sum - lower_sum) / upper);
            const double score = (lower / total) * (upper / total) * difference * difference;
            if (!observe(t, score)) {
                break;
            }
        }
        return {};
    }
};
core::Result<void> accumulate_histogram(image::PlaneView<const std::uint8_t> source,
                                        std::span<std::uint64_t> counts,
                                        const core::Cancellation& cancellation) {
    for (std::size_t first = 0; first < counts.size(); first += chunk) {
        if (cancellation.requested(core::Checkpoint::initialization)) {
            return core::cancelled();
        }
        std::ranges::fill(counts.subspan(first, chunk), 0);
    }
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        const auto row = source.row(y);
        for (std::size_t first = 0; first < row.size(); first += chunk) {
            if (cancellation.requested(core::Checkpoint::measurement)) {
                return core::cancelled();
            }
            for (const auto sample : row.subspan(first, std::min(chunk, row.size() - first))) {
                ++counts.subspan(quantize(sample), 1).front();
            }
        }
    }
    return {};
}
core::Result<OtsuObservation> select_threshold(std::span<const std::uint64_t> counts,
                                               std::uint64_t total_count,
                                               const core::Cancellation& cancellation) {
    unsigned occupied = 0;
    double sum = 0;
    for (std::uint32_t bin = 0; bin < otsu_histogram_bins; ++bin) {
        if (bin % chunk == 0 && cancellation.requested(core::Checkpoint::measurement)) {
            return core::cancelled();
        }
        const auto count = counts.subspan(bin, 1).front();
        occupied += static_cast<unsigned>(count != 0);
        // Do not multiply count by bin in uint64: legal plane dimensions exceed that bound.
        sum += static_cast<double>(count) * bin;
    }
    if (occupied == 1) {
        return OtsuObservation{
            .threshold_bin = otsu_fallback_threshold,
            .single_bin_fallback = true,
        };
    }
    const Histogram histogram{
        .counts = counts,
        .total_count = total_count,
        .total_sum = sum,
        .cancellation = cancellation,
    };
    double best = 0;
    const auto maximized = histogram.scores([&](std::uint32_t /*bin*/, double score) {
        best = std::max(best, score);
        return true;
    });
    if (!maximized) {
        return std::unexpected(maximized.error());
    }
    const double tolerance = relative_score_tolerance * std::max(1.0, best);
    OtsuObservation observation;
    // A second scan anchors tolerance to the true maximum, avoiding chained near-tie drift.
    const auto selected = histogram.scores([&](std::uint32_t bin, double score) {
        if (best - score <= tolerance) {
            observation.threshold_bin = static_cast<std::uint16_t>(bin);
            return false;
        }
        return true;
    });
    if (!selected) {
        return std::unexpected(selected.error());
    }
    return observation;
}
} // namespace
core::Result<OtsuObservation> fit_otsu(image::PlaneView<const std::uint8_t> source,
                                       core::Budget& budget,
                                       const core::Cancellation& cancellation) {
    if (source.empty()) {
        return core::failure(core::ErrorCode::argument, "B01 requires a nonempty source plane");
    }
    if (cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto workspace = image::Plane<std::uint64_t>::allocate(budget, otsu_histogram_bins, 1);
    if (!workspace) {
        return std::unexpected(workspace.error());
    }
    const auto counts = workspace->view().row(0);
    const auto accumulated = accumulate_histogram(source, counts, cancellation);
    if (!accumulated) {
        return std::unexpected(accumulated.error());
    }
    return select_threshold(counts, std::uint64_t{source.width()} * source.height(), cancellation);
}

core::Result<void> apply_otsu(image::PlaneView<const std::uint8_t> source,
                              image::PlaneView<std::uint8_t> destination,
                              const OtsuObservation& observation,
                              const core::Cancellation& cancellation) {
    if (!valid_planes(source, destination) || !valid_otsu(observation)) {
        return core::failure(core::ErrorCode::argument,
                             "B01 requires valid frozen observations and nonempty disjoint planes");
    }
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        const auto in = source.row(y);
        const auto out = destination.row(y);
        for (std::size_t first = 0; first < in.size(); first += chunk) {
            if (cancellation.requested(core::Checkpoint::processing)) {
                return core::cancelled();
            }
            const auto end = first + std::min(chunk, in.size() - first);
            for (auto x = first; x < end; ++x) {
                out.subspan(x, 1).front() =
                    quantize(in.subspan(x, 1).front()) <= observation.threshold_bin
                        ? 0
                        : static_cast<std::uint8_t>(white);
            }
        }
    }
    return {};
}
core::Result<OtsuObservation> otsu(image::PlaneView<const std::uint8_t> source,
                                   image::PlaneView<std::uint8_t> destination,
                                   BinarizationContext context) {
    if (!valid_planes(source, destination)) {
        return core::failure(core::ErrorCode::argument,
                             "B01 requires nonempty, equally sized, disjoint planes");
    }
    const auto& cancellation = context.scheduler.get().cancellation();
    auto observation = fit_otsu(source, context.budget.get(), cancellation);
    if (!observation) {
        return std::unexpected(observation.error());
    }
    const auto applied = apply_otsu(source, destination, *observation, cancellation);
    if (!applied) {
        return std::unexpected(applied.error());
    }
    return observation;
}
} // namespace docenhance::methods
