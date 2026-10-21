// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "method_schema_examples.hpp"
#include "schema_example.hpp"

#include <expected>
#include <utility>

namespace docenhance::tests {
namespace {
template <typename T> core::Result<record::Json> contrast_refusals(const T& facts) {
    auto invalid = record::Json::array();
    auto encoded = record::Codec::encode(facts);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    auto changed = *encoded;
    changed["method"] = "D01";
    invalid.push_back(changed);
    changed = *encoded;
    changed["reason"] = "processing_failure";
    invalid.push_back(changed);
    changed = *encoded;
    changed["progress"] = "completed";
    invalid.push_back(changed);
    changed = *encoded;
    changed["counts"]["eligible"] = true;
    invalid.push_back(changed);
    changed = *encoded;
    changed.erase("reason");
    invalid.push_back(changed);
    return invalid;
}

core::Result<void> contrast_examples(record::Json& result) {
    const methods::LevelsFacts levels{
        .reason = methods::LevelsReason::zero_blend,
        .counts = {.eligible = 8},
    };
    auto invalid_levels = contrast_refusals(levels);
    if (!invalid_levels) {
        return std::unexpected(std::move(invalid_levels.error()));
    }
    auto encoded = record::Codec::encode(levels);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    auto changed = *encoded;
    changed.erase("levels");
    invalid_levels->push_back(changed);
    if (auto added = schema_example(result, "levels facts", levels, std::move(*invalid_levels));
        !added) {
        return added;
    }
    const methods::LevelsFacts active_levels{
        .disposition = methods::Disposition::applied,
        .reason = methods::LevelsReason::none,
        .counts = {.eligible = 8, .evaluated = 8, .changed = 3},
        .levels = methods::LevelsRange{.low = 0.1, .high = 0.9},
    };
    auto active_levels_invalid = contrast_refusals(active_levels);
    if (!active_levels_invalid) {
        return std::unexpected(std::move(active_levels_invalid.error()));
    }
    if (auto added = schema_example(result, "active levels facts", active_levels,
                                    std::move(*active_levels_invalid));
        !added) {
        return added;
    }
    return {};
}
core::Result<void> gamma_clahe_examples(record::Json& result) {
    const methods::GammaFacts gamma{
        .disposition = methods::Disposition::applied,
        .reason = methods::GammaReason::none,
        .counts = {.eligible = 8, .evaluated = 8, .changed = 3},
    };
    auto gamma_invalid = contrast_refusals(gamma);
    if (!gamma_invalid) {
        return std::unexpected(std::move(gamma_invalid.error()));
    }
    if (auto added = schema_example(result, "gamma facts", gamma, std::move(*gamma_invalid));
        !added) {
        return added;
    }
    const methods::ClaheFacts clahe{
        .reason = methods::ClaheReason::zero_blend,
        .counts = {.eligible = 8},
    };
    auto invalid_clahe = contrast_refusals(clahe);
    if (!invalid_clahe) {
        return std::unexpected(std::move(invalid_clahe.error()));
    }
    auto encoded = record::Codec::encode(clahe);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    auto changed = *encoded;
    changed["identity_tiles"] = methods::clahe_tiles_max + 1;
    invalid_clahe->push_back(changed);
    if (auto added = schema_example(result, "clahe facts", clahe, std::move(*invalid_clahe));
        !added) {
        return added;
    }
    return {};
}

core::Result<void> sharpen_example(record::Json& result) {
    const methods::UnsharpFacts facts{
        .reason = methods::UnsharpReason::no_effect,
        .counts = {.eligible = 1, .protected_samples = 9, .evaluated = 1},
        .context_samples = 10,
        .corrected_samples = 1,
        .pre_clamp = methods::ExcursionRange{.low = -0.1, .high = -0.1},
        .clipped_low_samples = 1,
    };
    auto invalid = record::Json::array();
    auto encoded = record::Codec::encode(facts);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    auto changed = *encoded;
    changed["method"] = "C02";
    invalid.push_back(changed);
    changed = *encoded;
    changed["pre_clamp"]["high"] = 4;
    invalid.push_back(changed);
    changed = *encoded;
    changed.erase("pre_clamp");
    invalid.push_back(changed);
    changed = *encoded;
    changed["requested"] = record::Json::object();
    invalid.push_back(changed);
    changed = *encoded;
    changed["corrected_samples"] = true;
    invalid.push_back(changed);
    if (auto added = schema_example(result, "unsharp facts", facts, std::move(invalid)); !added) {
        return added;
    }
    return {};
}

} // namespace
core::Result<void> contrast_schema_examples(record::Json& result) {
    for (const auto emit : {contrast_examples, gamma_clahe_examples, sharpen_example}) {
        auto added = emit(result);
        if (!added) {
            return added;
        }
    }
    return {};
}
} // namespace docenhance::tests
