// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "binary_detail.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "docenhance/methods/binary_facts.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
namespace docenhance::methods {
std::uint32_t binary_otsu_bin(std::uint16_t sample) noexcept {
    constexpr std::uint32_t white = UINT16_MAX;
    // No half-integer occurs with the odd denominator; integer rounding is exact.
    return (((otsu_histogram_bins - 1) * sample) + (white / 2)) / white;
}
namespace {
constexpr double relative_score_tolerance = 1e-12;
constexpr std::size_t chunk = 1024;
struct BinaryThreshold {
    std::uint16_t threshold_bin = otsu_fallback_threshold;
    bool single_bin_fallback = false;
};
struct Histogram {
    std::span<const std::uint64_t> counts;
    std::uint64_t total_count = 0;
    double total_sum = 0;
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
core::Result<void> accumulate_histogram(image::PlaneView<const std::uint16_t> source,
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
                ++counts.subspan(binary_otsu_bin(sample), 1).front();
            }
        }
    }
    return {};
}
core::Result<BinaryThreshold> select_threshold(std::span<const std::uint64_t> counts,
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
        // Preserve the specified binary64 bin-moment evaluation order.
        sum += static_cast<double>(count) * bin;
    }
    if (occupied == 1) {
        return BinaryThreshold{
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
    BinaryThreshold observation;
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
core::Result<OtsuFacts> binarize(const Otsu& /*method*/, BinaryInput input) {
    if (auto valid = validate_binary_input(input); !valid) {
        return std::unexpected(valid.error());
    }
    const auto cancellation = input.scheduler.get().cancellation();
    if (cancellation.requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    auto histogram =
        image::Plane<std::uint64_t>::allocate(input.budget.get(), otsu_histogram_bins, 1);
    if (!histogram) {
        return std::unexpected(histogram.error());
    }
    auto counts = binary_counts(input, BinaryPartition::rows);
    if (!counts) {
        return std::unexpected(counts.error());
    }
    BinaryThreshold threshold;
    const auto fit = [&](std::size_t) -> core::Result<void> {
        if (auto gathered =
                accumulate_histogram(input.codes, histogram->view().row(0), cancellation);
            !gathered) {
            return gathered;
        }
        auto selected = select_threshold(histogram->view().row(0),
                                         std::uint64_t{input.codes.width()} * input.codes.height(),
                                         cancellation);
        if (!selected) {
            return std::unexpected(selected.error());
        }
        threshold = *selected;
        return {};
    };
    if (auto fitted = input.scheduler.get().for_each(1, exec::WorkRef{fit}); !fitted) {
        return std::unexpected(fitted.error());
    }
    histogram.value() = image::Plane<std::uint64_t>{};
    const auto classify = [&](std::uint16_t value) noexcept {
        return binary_otsu_bin(value) <= threshold.threshold_bin;
    };
    auto classified = binary_rows(input, counts->view(), classify);
    if (!classified) {
        return std::unexpected(classified.error());
    }
    OtsuFacts facts;
    static_cast<ClassificationFacts&>(facts) = *classified;
    facts.threshold_bin = threshold.threshold_bin;
    facts.single_bin_fallback = threshold.single_bin_fallback;
    if (!validate(facts)) {
        return core::failure(core::ErrorCode::invariant, "Invalid completed Otsu facts");
    }
    return facts;
}
} // namespace docenhance::methods
