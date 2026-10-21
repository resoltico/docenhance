// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/working.hpp"
#include "morphology_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

namespace docenhance::methods {
inline constexpr std::size_t morphology_bands_max = 64;
inline std::size_t morphology_bands(std::uint32_t lines) noexcept {
    return std::min(morphology_bands_max, static_cast<std::size_t>(lines));
}
inline std::pair<std::uint32_t, std::uint32_t> morphology_lines(std::uint32_t lines,
                                                                std::size_t band) noexcept {
    const auto bands = morphology_bands(lines);
    return {static_cast<std::uint32_t>(std::uint64_t{band} * lines / bands),
            static_cast<std::uint32_t>(std::uint64_t{band + 1} * lines / bands)};
}
template <class Sample>
core::Result<image::Plane<Sample>> morphology_allocate(Working working, std::uint32_t width,
                                                       std::uint32_t height) {
    if (working.scheduler.get().cancellation().requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    return image::Plane<Sample>::allocate(working.budget.get(), width, height);
}
struct MorphologySourceAnalysis {
    MorphologySampling sampling;
    double fill{};
};
struct MorphologyFit {
    image::Plane<double> background;
    MorphologyAnalysis analysis;
};
[[nodiscard]] core::Result<MorphologySampling>
sample_morphology(Working working, image::PlaneView<const double> input, std::span<double> values);
[[nodiscard]] core::Result<MorphologySourceAnalysis> analyze_morphology(Working working,
                                                                        std::uint64_t eligible);
[[nodiscard]] core::Result<MorphologyAnalysis>
measure_morphology(Working working, image::PlaneView<const double> background,
                   const MorphologySourceAnalysis& source, std::uint64_t eligible);
[[nodiscard]] core::Result<MorphologyFit> fit_morphology(const Morphology& method, Working working,
                                                         std::uint64_t eligible);
} // namespace docenhance::methods
