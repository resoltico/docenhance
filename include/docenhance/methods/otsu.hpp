// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/binarization.hpp"

#include <cstddef>
#include <cstdint>

namespace docenhance::methods {
inline constexpr std::uint32_t otsu_histogram_bins = 4096;
inline constexpr std::uint16_t otsu_fallback_threshold = (otsu_histogram_bins - 1) / 2;
inline constexpr std::uint16_t otsu_max_threshold = otsu_histogram_bins - 2;
inline constexpr std::size_t otsu_scratch_bytes = otsu_histogram_bins * sizeof(std::uint64_t);
struct OtsuObservation {
    std::uint16_t threshold_bin = otsu_fallback_threshold;
    bool single_bin_fallback = false;
    bool operator==(const OtsuObservation&) const = default;
};
[[nodiscard]] inline bool valid_otsu(const OtsuObservation& observation) noexcept {
    return observation.threshold_bin <= otsu_max_threshold &&
           (!observation.single_bin_fallback ||
            observation.threshold_bin == otsu_fallback_threshold);
}
// Fit the global 4096-bin histogram once on stored uint8 grayscale samples. Scratch is charged
// and refunded before return. Empty views are invalid; no geometry mask or color transform exists.
[[nodiscard]] core::Result<OtsuObservation> fit_otsu(image::PlaneView<const std::uint8_t> source,
                                                     core::Budget& budget,
                                                     const core::Cancellation& cancellation = {});
// Reuse the frozen fit: round(4095*p/255) <= threshold_bin is exactly black (0), else white (255).
// Equally sized nonempty planes must be disjoint. Cancellation can leave partial internal output.
[[nodiscard]] core::Result<void> apply_otsu(image::PlaneView<const std::uint8_t> source,
                                            image::PlaneView<std::uint8_t> destination,
                                            const OtsuObservation& observation,
                                            const core::Cancellation& cancellation = {});
[[nodiscard]] core::Result<OtsuObservation> otsu(image::PlaneView<const std::uint8_t> source,
                                                 image::PlaneView<std::uint8_t> destination,
                                                 BinarizationContext context);
} // namespace docenhance::methods
