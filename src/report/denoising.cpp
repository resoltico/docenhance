// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "denoising.hpp"

#include "docenhance/core/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/record/codec.hpp"

#include <expected>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::report {
core::Result<std::string> denoising_text(const std::optional<methods::DenoisingFacts>& facts) {
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
            std::string text = "Denoising " + std::string(core::WireTag<Facts>::value) + ": " +
                               encoded->at("disposition").get<std::string>() + " (" +
                               encoded->at("reason").get<std::string>() +
                               "); counts=" + encoded->at("counts").dump() +
                               "; corrected=" + std::to_string(actual.corrected_samples) + "\n";
            if constexpr (std::is_same_v<Facts, methods::NlmFacts>) {
                text += "NLM weight table entries: " + std::to_string(actual.weight_table_entries) +
                        "\n";
            } else {
                text += "TV-L1 context samples: " + std::to_string(actual.context_samples) + "\n";
                if (actual.solver) {
                    const auto& solver = *actual.solver;
                    text += "TV-L1: iterations=" + std::to_string(solver.iterations) +
                            "; primal update=" + encoded->at("solver").at("primal_update").dump() +
                            "; dual update=" + encoded->at("solver").at("dual_update").dump() +
                            "\n";
                    text +=
                        "TV-L1 objective: " + encoded->at("solver").at("objective_start").dump() +
                        " -> " + encoded->at("solver").at("objective_end").dump() + "\n";
                    text +=
                        "TV-L1 stopping: " + encoded->at("solver").at("stop").get<std::string>() +
                        "; passing checkpoints=" + std::to_string(solver.passing_checkpoints) +
                        "\n";
                }
            }
            return text;
        },
        *facts);
}
} // namespace docenhance::report
