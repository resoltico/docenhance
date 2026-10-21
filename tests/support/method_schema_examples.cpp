// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "method_schema_examples.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "morphology_fixture.hpp"
#include "restoration_fixture.hpp"
#include "schema_example.hpp"
#include "stage_report_fixture.hpp"
#include "surface_fixture.hpp"

#include <expected>
#include <utility>

namespace docenhance::tests {
namespace {
template <typename T> core::Result<record::Json> binary_refusals(const T& facts) {
    auto invalid = record::Json::array();
    auto encoded = record::Codec::encode(facts);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    auto changed = *encoded;
    changed["disposition"] = "no_change";
    invalid.push_back(changed);
    changed = *encoded;
    changed["reason"] = "no_effect";
    invalid.push_back(changed);
    changed = *encoded;
    changed["black_samples"] = true;
    invalid.push_back(changed);
    changed = *encoded;
    changed["protected_samples"] = 1;
    invalid.push_back(changed);
    return invalid;
}
core::Result<void> binary_examples(record::Json& result) {
    methods::OtsuFacts global;
    global.black_samples = 3;
    global.white_samples = 5;
    global.threshold_bin = 100;
    auto global_invalid = binary_refusals(global);
    if (!global_invalid) {
        return std::unexpected(std::move(global_invalid.error()));
    }
    if (auto added = schema_example(result, "otsu facts", global, std::move(*global_invalid));
        !added) {
        return added;
    }
    methods::SauvolaFacts local;
    local.white_samples = 8;
    auto local_invalid = binary_refusals(local);
    if (!local_invalid) {
        return std::unexpected(std::move(local_invalid.error()));
    }
    if (auto added = schema_example(result, "sauvola facts", local, std::move(*local_invalid));
        !added) {
        return added;
    }
    methods::FixedFacts fixed;
    fixed.black_samples = 8;
    auto fixed_invalid = binary_refusals(fixed);
    if (!fixed_invalid) {
        return std::unexpected(std::move(fixed_invalid.error()));
    }
    if (auto added = schema_example(result, "fixed facts", fixed, std::move(*fixed_invalid));
        !added) {
        return added;
    }
    return {};
}

core::Result<void> nlm_example(record::Json& result) {
    const auto nlm = methods::Nlm::create();
    if (!nlm) {
        return std::unexpected(nlm.error());
    }
    const methods::NlmFacts facts{
        .disposition = methods::Disposition::applied,
        .reason = methods::NlmReason::none,
        .counts = {.eligible = 441, .evaluated = 441, .changed = 100},
        .corrected_samples = 120,
        .weight_table_entries = methods::nlm_weight_entries(*nlm),
    };
    auto invalid = record::Json::array();
    auto encoded = record::Codec::encode(facts);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    auto changed = *encoded;
    changed["weight_table_entries"] = true;
    invalid.push_back(changed);
    changed["weight_table_entries"] = -1;
    invalid.push_back(changed);
    changed = *encoded;
    changed["corrected_samples"] = true;
    invalid.push_back(changed);
    changed = *encoded;
    changed["native_h"] = 771;
    invalid.push_back(changed);
    changed = *encoded;
    changed["progress"] = "completed";
    invalid.push_back(changed);
    changed = *encoded;
    changed["method"] = "D02";
    invalid.push_back(changed);
    if (auto added = schema_example(result, "nlm facts", facts, std::move(invalid)); !added) {
        return added;
    }
    const methods::NlmFacts no_effect{
        .reason = methods::NlmReason::no_effect,
        .counts = {.eligible = 441, .evaluated = 441},
        .weight_table_entries = methods::nlm_weight_entries(*nlm),
    };
    if (auto added = schema_example(result, "nlm no-effect facts", no_effect); !added) {
        return added;
    }
    return {};
}

core::Result<void> tvl1_examples(record::Json& result) {
    const methods::Tvl1Facts active{
        .disposition = methods::Disposition::applied,
        .reason = methods::Tvl1Reason::none,
        .counts = {.eligible = 8, .evaluated = 8, .changed = 2},
        .context_samples = 8,
        .corrected_samples = 3,
        .solver =
            methods::Tvl1Solution{
                .iterations = 30,
                .stop = methods::Tvl1Stop::tolerance_met,
                .passing_checkpoints = 2,
                .primal_update = 1e-6,
                .dual_update = 2e-6,
                .objective_start = 8,
                .objective_end = 7,
            },
    };
    auto invalid = record::Json::array();
    auto encoded = record::Codec::encode(active);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    auto changed = *encoded;
    changed["solver"]["stop"] = "processing_failure";
    invalid.push_back(changed);
    changed = *encoded;
    changed["solver"]["objective_end"] = -1;
    invalid.push_back(changed);
    changed = *encoded;
    changed["solver"]["passing_checkpoints"] = 3;
    invalid.push_back(changed);
    changed = *encoded;
    changed["solver"]["iterations"] = true;
    invalid.push_back(changed);
    changed = *encoded;
    changed.erase("solver");
    invalid.push_back(changed);
    changed = *encoded;
    changed["native_h"] = 771;
    invalid.push_back(changed);
    if (auto added = schema_example(result, "tvl1 active facts", active, std::move(invalid));
        !added) {
        return added;
    }
    const methods::Tvl1Facts bypass{
        .reason = methods::Tvl1Reason::zero_blend,
        .counts = {.eligible = 8},
    };
    if (auto added = schema_example(result, "tvl1 bypass facts", bypass); !added) {
        return added;
    }
    return {};
}

core::Result<void> morphology_examples(record::Json& result) {
    const auto active = completed_morphology_facts();
    auto invalid = record::Json::array();
    auto encoded = record::Codec::encode(active);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    auto changed = *encoded;
    changed["method"] = "I01";
    invalid.push_back(changed);
    changed = *encoded;
    changed["analysis"]["analysis_fill"] = -1;
    invalid.push_back(changed);
    changed = *encoded;
    changed["resolved_radius"] = true;
    invalid.push_back(changed);
    changed = *encoded;
    changed["solver"] = nullptr;
    invalid.push_back(changed);
    changed = *encoded;
    changed.erase("analysis");
    invalid.push_back(changed);
    changed = *encoded;
    changed["reason"] = "automatic_predicates";
    invalid.push_back(changed);
    if (auto added = schema_example(result, "morphology active facts", active, std::move(invalid));
        !added) {
        return added;
    }
    const methods::MorphologyFacts bypass{
        .reason = methods::MorphologyReason::zero_strength,
        .counts = {.eligible = 256},
        .resolved_radius = 8,
        .sigma = 4,
        .gaussian_radius = 12,
    };
    if (auto added = schema_example(result, "morphology bypass facts", bypass); !added) {
        return added;
    }
    if (auto added = schema_example(result, "morphology no-effect facts", black_morphology_facts());
        !added) {
        return added;
    }
    return {};
}

core::Result<void> surface_examples(record::Json& result) {
    const auto facts = completed_surface_facts();
    auto invalid = record::Json::array();
    auto encoded = record::Codec::encode(facts);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    auto changed = *encoded;
    changed["method"] = "I02";
    invalid.push_back(changed);
    changed = *encoded;
    changed["auto_predicates"]["coverage"] = 1;
    invalid.push_back(changed);
    changed = *encoded;
    changed.erase("solver");
    invalid.push_back(changed);
    changed = *encoded;
    changed["solver"]["iterations"] = 501;
    invalid.push_back(changed);
    changed = *encoded;
    changed["measurements"]["background_q10"] = -1;
    invalid.push_back(changed);
    changed = *encoded;
    changed["requested"] = record::Json::object();
    invalid.push_back(changed);
    if (auto added = schema_example(result, "surface active facts", facts, std::move(invalid));
        !added) {
        return added;
    }
    if (auto added = schema_example(result, "surface no-effect facts", singleton_surface_facts());
        !added) {
        return added;
    }
    const methods::SurfaceFacts bypass{
        .reason = methods::SurfaceReason::zero_strength,
        .counts = {.eligible = 4096},
    };
    if (auto added = schema_example(result, "surface bypass facts", bypass); !added) {
        return added;
    }
    return {};
}
core::Result<void> restoration_examples(record::Json& result) {
    const auto facts = completed_wiener_facts();
    auto invalid = record::Json::array();
    auto encoded = record::Codec::encode(facts);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    auto changed = *encoded;
    changed["psf"]["coefficients"] = record::Json::array({1});
    invalid.push_back(changed);
    changed = *encoded;
    changed["psf"]["path"] = "private/kernel.png";
    invalid.push_back(changed);
    changed = *encoded;
    changed["transform"]["width"] = true;
    invalid.push_back(changed);
    changed = *encoded;
    changed["native_calls"] = 3;
    invalid.push_back(changed);
    changed = *encoded;
    changed["psf"]["kind"] = "blind";
    invalid.push_back(changed);
    if (auto added = schema_example(result, "wiener active facts", facts, std::move(invalid));
        !added) {
        return added;
    }
    const methods::WienerFacts bypass{
        .reason = methods::WienerReason::zero_blend,
        .counts = {.eligible = 128},
        .psf = analytic_psf_facts(),
    };
    if (auto added = schema_example(result, "wiener bypass facts", bypass); !added) {
        return added;
    }
    if (auto added = schema_example(result, "wiener no-effect facts", tiny_blend_wiener_facts());
        !added) {
        return added;
    }
    const methods::WienerFacts kernel{
        .reason = methods::WienerReason::zero_blend,
        .counts = {.eligible = 128},
        .psf = edge_psf_facts(129, 129, false, true),
    };
    if (auto added = schema_example(result, "wiener kernel edge facts", kernel); !added) {
        return added;
    }
    return {};
}
core::Result<void> stage_examples(record::Json& result) {
    const auto reports = selected_stage_reports();
    auto invalid = record::Json::array();
    auto encoded = record::Codec::encode(reports);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    auto changed = *encoded;
    changed.erase("illumination");
    invalid.push_back(changed);
    changed = *encoded;
    changed["unknown_stage"] = nullptr;
    invalid.push_back(changed);
    if (auto added = schema_example(result, "stage reports", reports, std::move(invalid)); !added) {
        return added;
    }
    methods::CompletedStages completed;
    completed.denoising = reports.denoising;
    auto invalid_completed = record::Json::array();
    encoded = record::Codec::encode(completed);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    changed = *encoded;
    changed["illumination"] = nullptr;
    invalid_completed.push_back(changed);
    changed = *encoded;
    changed["unknown_stage"] = nullptr;
    invalid_completed.push_back(changed);
    if (auto added =
            schema_example(result, "completed stages", completed, std::move(invalid_completed));
        !added) {
        return added;
    }
    if (auto added = schema_example(result, "empty completed stages", methods::CompletedStages{});
        !added) {
        return added;
    }
    return {};
}
} // namespace
core::Result<void> method_schema_examples(record::Json& result) {
    for (const auto emit : {
             contrast_schema_examples,
             binary_examples,
             nlm_example,
             tvl1_examples,
             morphology_examples,
             surface_examples,
             restoration_examples,
             stage_examples,
         }) {
        auto added = emit(result);
        if (!added) {
            return added;
        }
    }
    return {};
}
} // namespace docenhance::tests
