// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/warnings.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/stage_reports.hpp"

#include <array>
#include <optional>

namespace docenhance::methods {
// Runtime-only availability from actual entry/completion. No Fields/wire binding is permitted.
// Completed stages own their Facts; this retains only still-partial warning context.
struct FailureWarningContext {
    std::optional<image::InterpretationReport> interpretation;
    std::optional<Tvl1Stop> observed_tvl1_stop;
    bool restoration_entered = false;
    bool restoration_after_transform = false;
    std::optional<std::array<double, 2>> resolved_psf_centroid;
    bool positive_sharpening_entered = false;
};

// Call only after Facts and their actual request/extent context have been admitted.
// These completed-report projections do not recover unstarted or partial failure diagnostics.
[[nodiscard]] bool transformed_before_restoration(const StageReports& reports,
                                                  image::Orientation orientation,
                                                  image::QuarterTurn rotation) noexcept;
[[nodiscard]] core::WarningList warnings(const SurfaceFacts& facts) noexcept;
[[nodiscard]] core::WarningList warnings(const MorphologyFacts& facts) noexcept;
[[nodiscard]] core::WarningList warnings(const NlmFacts& facts) noexcept;
[[nodiscard]] core::WarningList warnings(const Tvl1Facts& facts) noexcept;
[[nodiscard]] core::WarningList warnings(const WienerFacts& facts, bool after_transform) noexcept;
[[nodiscard]] core::WarningList warnings(const LevelsFacts& facts) noexcept;
[[nodiscard]] core::WarningList warnings(const GammaFacts& facts) noexcept;
[[nodiscard]] core::WarningList warnings(const ClaheFacts& facts) noexcept;
[[nodiscard]] core::WarningList warnings(const UnsharpFacts& facts) noexcept;
[[nodiscard]] core::WarningList warnings(const OtsuFacts& facts) noexcept;
[[nodiscard]] core::WarningList warnings(const SauvolaFacts& facts) noexcept;
[[nodiscard]] core::WarningList warnings(const FixedFacts& facts) noexcept;
} // namespace docenhance::methods
