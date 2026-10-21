// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/tvl1_facts.hpp"

#include <optional>
#include <tuple>
#include <variant>

namespace docenhance::methods {
using IlluminationFacts = std::variant<SurfaceFacts, MorphologyFacts>;
using DenoisingFacts = std::variant<NlmFacts, Tvl1Facts>;
using ContrastFacts = std::variant<LevelsFacts, GammaFacts, ClaheFacts>;
using BinarizationFacts = std::variant<OtsuFacts, SauvolaFacts, FixedFacts>;
struct StageReports {
    std::optional<IlluminationFacts> illumination = std::nullopt;
    std::optional<DenoisingFacts> denoising = std::nullopt;
    std::optional<WienerFacts> restoration = std::nullopt;
    std::optional<ContrastFacts> contrast = std::nullopt;
    std::optional<UnsharpFacts> sharpening = std::nullopt;
    std::optional<BinarizationFacts> binarization = std::nullopt;
    bool operator==(const StageReports&) const = default;
};
// Failure's completed map has the same owned facts, with a distinct closed sparse wire policy.
struct CompletedStages : StageReports {
    bool operator==(const CompletedStages&) const = default;
};
[[nodiscard]] bool validate(const StageReports& reports) noexcept;
[[nodiscard]] bool validate(const CompletedStages& reports) noexcept;
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::Fields<methods::StageReports> {
    using Value = methods::StageReports;
    static constexpr auto list = std::tuple{
        core::field("illumination", &Value::illumination),
        core::field("denoising", &Value::denoising),
        core::field("restoration", &Value::restoration),
        core::field("contrast", &Value::contrast),
        core::field("sharpening", &Value::sharpening),
        core::field("binarization", &Value::binarization),
    };
};
template <> struct core::Fields<methods::CompletedStages> {
    static constexpr auto list = core::Fields<methods::StageReports>::list;
};
template <> inline constexpr bool core::sparse_fields<methods::CompletedStages> = true;
} // namespace docenhance
