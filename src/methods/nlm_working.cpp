// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/nlm_working.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/working.hpp"
#include "nlm_detail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <utility>
namespace docenhance::methods {
namespace {
core::Result<NlmObservation> evaluate_band(const NlmContext& context, NlmBand band) {
    auto storage = nlm_storage(context, band);
    if (!storage) {
        return std::unexpected(std::move(storage.error()));
    }
    const auto radius =
        static_cast<std::int32_t>((context.method.get().parameters().search - 1) / 2);
    for (auto oy = -radius; oy <= radius; ++oy) {
        for (auto ox = -radius; ox <= radius; ++ox) {
            const NlmOffset offset{.x = ox, .y = oy};
            if (auto measured = nlm_distances(context, band, offset, *storage); !measured) {
                return std::unexpected(std::move(measured.error()));
            }
            if (auto summed = nlm_accumulate(context, band, offset, *storage); !summed) {
                return std::unexpected(std::move(summed.error()));
            }
        }
    }
    return nlm_map(context, band, *storage);
}
core::Result<NlmFacts> completed(const Nlm& method, Working working, NlmFacts facts) {
    if (!validate(facts, method.parameters()) ||
        facts.counts.eligible + facts.counts.protected_samples !=
            std::uint64_t{working.luminance.width()} * working.luminance.height()) {
        return core::failure(core::ErrorCode::invariant, "NLM produced invalid completed facts");
    }
    return facts;
}
core::Result<NlmFacts> execute(const Nlm& method, Working working, NlmFacts facts) {
    auto prepared = prepare_nlm(method, working);
    if (!prepared) {
        return std::unexpected(std::move(prepared.error()));
    }
    const NlmContext context{method, working, *prepared};
    const auto task = [&](std::size_t index) -> core::Result<void> {
        const auto band = nlm_band(prepared->codes.height(), static_cast<std::uint32_t>(index));
        auto result = evaluate_band(context, band);
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        prepared->observations.view().row(0).subspan(index, 1).front() = result->evaluated;
        prepared->observations.view().row(1).subspan(index, 1).front() = result->corrected;
        prepared->observations.view().row(2).subspan(index, 1).front() = result->changed;
        return {};
    };
    const auto bands = nlm_band_count(prepared->codes.height());
    if (auto mapped = working.scheduler.get().for_each(bands, exec::WorkRef{task}); !mapped) {
        return std::unexpected(std::move(mapped.error()));
    }
    const auto cancellation = working.scheduler.get().cancellation();
    for (std::uint32_t index = 0; index < bands; ++index) {
        if (index % nlm_checkpoint_samples == 0 &&
            cancellation.requested(core::Checkpoint::processing)) {
            return core::cancelled();
        }
        facts.counts.evaluated += prepared->observations.view().row(0).subspan(index, 1).front();
        facts.corrected_samples += prepared->observations.view().row(1).subspan(index, 1).front();
        facts.counts.changed += prepared->observations.view().row(2).subspan(index, 1).front();
    }
    facts.weight_table_entries = prepared->weights.width();
    facts.disposition = facts.counts.changed == 0 ? Disposition::no_change : Disposition::applied;
    facts.reason = facts.counts.changed == 0 ? NlmReason::no_effect : NlmReason::none;
    return completed(method, working, facts);
}
} // namespace
core::Result<NlmFacts> apply(const Nlm& method, Working working) {
    auto counts = eligibility(working);
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    NlmFacts facts{.counts = *counts};
    if (method.parameters().blend == 0) {
        facts.reason = NlmReason::zero_blend;
        return completed(method, working, facts);
    }
    if (counts->eligible == 0) {
        return completed(method, working, facts);
    }
    if (method.parameters().search >
        std::min(working.luminance.width(), working.luminance.height())) {
        return core::failure(core::ErrorCode::method_inapplicable,
                             "NLM search must fit the oriented image");
    }
    return execute(method, working, facts);
}
} // namespace docenhance::methods
