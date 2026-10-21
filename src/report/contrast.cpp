// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "contrast.hpp"

#include "docenhance/core/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/record/codec.hpp"

#include <expected>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::report {
core::Result<std::string> contrast_text(const std::optional<methods::ContrastFacts>& facts) {
    if (!facts) {
        return std::string{};
    }
    auto encoded = record::Codec::encode(*facts);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    return std::visit(
        [&](const auto& actual) {
            using Facts = std::decay_t<decltype(actual)>;
            std::string text = "Contrast " + std::string(core::WireTag<Facts>::value) + ": " +
                               encoded->at("disposition").get<std::string>() + " (" +
                               encoded->at("reason").get<std::string>() +
                               "); counts=" + encoded->at("counts").dump() + "\n";
            if constexpr (std::is_same_v<Facts, methods::LevelsFacts>) {
                if (actual.levels) {
                    text += "Levels: lo=" + encoded->at("levels").at("low").dump() +
                            "; hi=" + encoded->at("levels").at("high").dump() +
                            "; clipped low=" + std::to_string(actual.clipped_low_samples) +
                            "; clipped high=" + std::to_string(actual.clipped_high_samples) + "\n";
                }
            } else if constexpr (std::is_same_v<Facts, methods::ClaheFacts>) {
                text += "CLAHE: identity tiles=" + std::to_string(actual.identity_tiles) + "\n";
            }
            return text;
        },
        *facts);
}
} // namespace docenhance::report
