// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "restoration.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/record/codec.hpp"

#include <expected>
#include <optional>
#include <string>
#include <utility>
namespace docenhance::report {
core::Result<std::string> restoration_text(const std::optional<methods::WienerFacts>& facts) {
    if (!facts) {
        return std::string{};
    }
    auto encoded = record::Codec::encode(*facts);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    const auto& actual = *facts;
    std::string text = "Restoration " + std::string(core::WireTag<methods::WienerFacts>::value) +
                       ": " + encoded->at("disposition").get<std::string>() + " (" +
                       encoded->at("reason").get<std::string>() +
                       "); counts=" + encoded->at("counts").dump() +
                       "; context=" + std::to_string(actual.context_samples) +
                       "; corrected=" + std::to_string(actual.corrected_samples) + "\n";
    text += "PSF: " + encoded->at("psf").at("kind").get<std::string>() + " " +
            std::to_string(actual.psf.width) + "x" + std::to_string(actual.psf.height) +
            "; normalized sum=" + encoded->at("psf").at("normalized_sum").dump() +
            "; centroid=" + encoded->at("psf").at("centroid_x").dump() + "," +
            encoded->at("psf").at("centroid_y").dump() + "\n";
    if (actual.transform) {
        const auto& transform = *actual.transform;
        text += "Restoration transform: " + std::to_string(transform.width) + "x" +
                std::to_string(transform.height) + "; guard=" + std::to_string(transform.guard) +
                "; padded mean=" + encoded->at("transform").at("padded_mean").dump() + "\n";
    }
    text += "Restoration excursions: raw below zero=" + std::to_string(actual.raw_low_samples) +
            "; above one=" + std::to_string(actual.raw_high_samples) +
            "; blended below zero=" + std::to_string(actual.blended_low_samples) +
            "; above one=" + std::to_string(actual.blended_high_samples) + "\n";
    return text;
}
} // namespace docenhance::report
