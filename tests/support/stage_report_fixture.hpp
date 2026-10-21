// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/stage_reports.hpp"

namespace docenhance::tests {
inline methods::StageReports selected_stage_reports() {
    return {
        .denoising =
            methods::NlmFacts{
                .disposition = methods::Disposition::applied,
                .reason = methods::NlmReason::none,
                .counts = {.eligible = 512, .evaluated = 512, .changed = 100},
                .corrected_samples = 120,
                .weight_table_entries = 113337,
            },
        .contrast =
            methods::GammaFacts{
                .disposition = methods::Disposition::applied,
                .reason = methods::GammaReason::none,
                .counts = {.eligible = 512, .evaluated = 512, .changed = 50},
            },
    };
}
} // namespace docenhance::tests
