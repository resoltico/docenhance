// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/warnings.hpp"

#include "docenhance/core/warnings.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/tvl1_facts.hpp"

#include <array>
#include <cmath>
#include <optional>
#include <variant>

namespace docenhance::methods {
namespace {
template <typename... Facts>
bool changed(const std::optional<std::variant<Facts...>>& report) noexcept {
    return report && !report->valueless_by_exception() &&
           ((std::get_if<Facts>(&*report) != nullptr &&
             std::get_if<Facts>(&*report)->counts.changed != 0) ||
            ...);
}
} // namespace
bool transformed_before_restoration(const StageReports& reports, image::Orientation orientation,
                                    image::QuarterTurn rotation) noexcept {
    return orientation != image::Orientation::normal() ||
           rotation != image::QuarterTurn::identity() || changed(reports.illumination) ||
           changed(reports.denoising);
}
core::WarningList warnings(const Tvl1Facts& facts) noexcept {
    core::WarningList result{};
    if (facts.solver && facts.solver->stop == Tvl1Stop::iteration_limit) {
        result.front() = core::Warning::tv_iteration_limit;
    }
    return result;
}
core::WarningList warnings(const WienerFacts& facts, bool after_transform) noexcept {
    core::WarningList result{};
    result.front() = core::Warning::restoration_inference;
    if (after_transform) {
        std::get<1>(result) = core::Warning::psf_after_transform;
    }
    if (std::hypot(facts.psf.centroid_x, facts.psf.centroid_y) > psf_off_center_limit) {
        std::get<2>(result) = core::Warning::psf_off_center;
    }
    return result;
}
core::WarningList warnings(const UnsharpFacts& facts) noexcept {
    core::WarningList result{};
    if (facts.reason != UnsharpReason::zero_amount) {
        result.front() = core::Warning::sharpening;
    }
    return result;
}
core::WarningList warnings(const SurfaceFacts& /*facts*/) noexcept {
    return {};
}
core::WarningList warnings(const MorphologyFacts& /*facts*/) noexcept {
    return {};
}
core::WarningList warnings(const NlmFacts& /*facts*/) noexcept {
    return {};
}
core::WarningList warnings(const LevelsFacts& /*facts*/) noexcept {
    return {};
}
core::WarningList warnings(const GammaFacts& /*facts*/) noexcept {
    return {};
}
core::WarningList warnings(const ClaheFacts& /*facts*/) noexcept {
    return {};
}
core::WarningList warnings(const OtsuFacts& /*facts*/) noexcept {
    return {};
}
core::WarningList warnings(const SauvolaFacts& /*facts*/) noexcept {
    return {};
}
core::WarningList warnings(const FixedFacts& /*facts*/) noexcept {
    return {};
}
} // namespace docenhance::methods
