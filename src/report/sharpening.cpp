// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "sharpening.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/record/codec.hpp"

#include <expected>
#include <optional>
#include <string>
#include <utility>
namespace docenhance::report {
core::Result<std::string> sharpening_text(const std::optional<methods::UnsharpFacts>& facts) {
    if (!facts) {
        return std::string{};
    }
    auto encoded = record::Codec::encode(*facts);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    const auto& actual = *facts;
    std::string text = "Sharpening " + std::string(core::WireTag<methods::UnsharpFacts>::value) +
                       ": " + encoded->at("disposition").get<std::string>() + " (" +
                       encoded->at("reason").get<std::string>() +
                       "); counts=" + encoded->at("counts").dump() +
                       "; context=" + std::to_string(actual.context_samples) +
                       "; corrected=" + std::to_string(actual.corrected_samples) + "\n";
    if (actual.pre_clamp) {
        text += "Pre-clamp perceptual range: " + encoded->at("pre_clamp").at("low").dump() + ".." +
                encoded->at("pre_clamp").at("high").dump() + "\n";
    }
    text += "Sharpening excursions: below zero=" + std::to_string(actual.clipped_low_samples) +
            "; above one=" + std::to_string(actual.clipped_high_samples) + "\n";
    return text;
}
} // namespace docenhance::report
