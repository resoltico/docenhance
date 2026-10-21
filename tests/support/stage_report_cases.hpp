// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/contrast_facts.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/warnings.hpp"
#include "morphology_fixture.hpp"
#include "require.hpp"
#include "stage_report_fixture.hpp"

#include <exception>

namespace docenhance::tests {
struct ReportConstructionFailure : std::exception {};
struct ThrowingSurfaceFacts {
    explicit operator methods::SurfaceFacts() const {
        throw ReportConstructionFailure{};
    }
};
inline void valueless_stage_report_cases() {
    methods::StageReports reports;
    reports.illumination = completed_morphology_facts();
    require(methods::validate(reports),
            "the throwing-emplace control starts with valid owned facts");
    const auto retained = reports;
    bool caught = false;
    try {
        reports.illumination->emplace<methods::SurfaceFacts>(ThrowingSurfaceFacts{});
    } catch (const ReportConstructionFailure&) {
        caught = true;
    }
    require(caught, "the real report alternative construction must throw");
    if (reports.illumination->valueless_by_exception()) {
        require(!methods::validate(reports), "raw valueless stage reports are refused safely");
        methods::CompletedStages completed;
        completed.illumination = reports.illumination;
        require(!methods::validate(completed),
                "sparse completed facts also refuse a valueless family");
        require(!methods::transformed_before_restoration(reports, image::Orientation::normal(),
                                                         image::QuarterTurn::identity()),
                "the transform reader never enters get_if on a valueless prefix");
    } else {
        require(reports == retained && methods::validate(reports),
                "a library providing the strong guarantee preserves the exact healthy old facts");
    }
}
inline void stage_report_cases() {
    valueless_stage_report_cases();
    methods::StageReports reports;
    require(methods::validate(reports), "an empty stage set has no fabricated disabled reports");
    reports = selected_stage_reports();
    require(methods::validate(reports),
            "selected closed-family reports share one immutable pixel cohort");
    methods::FixedFacts binary;
    binary.black_samples = 128;
    binary.white_samples = 384;
    reports.binarization = binary;
    require(methods::validate(reports), "terminal classification covers the same full canvas");
    binary.white_samples = 383;
    reports.binarization = binary;
    require(!methods::validate(reports),
            "binary classification cannot silently lose a canvas pixel");
    reports = selected_stage_reports();
    reports.contrast = methods::GammaFacts{
        .disposition = methods::Disposition::applied,
        .reason = methods::GammaReason::none,
        .counts = {.eligible = 512, .protected_samples = 1, .evaluated = 512, .changed = 50},
    };
    require(!methods::validate(reports), "photometric stages cannot change protection population");
    reports.contrast = methods::GammaFacts{
        .disposition = methods::Disposition::applied,
        .reason = methods::GammaReason::none,
        .counts = {.eligible = 511, .protected_samples = 1, .evaluated = 511, .changed = 50},
    };
    require(!methods::validate(reports),
            "equal canvas totals do not excuse a changed eligible/protected cohort");
    reports = methods::StageReports{};
    reports.contrast = methods::GammaFacts{
        .reason = methods::GammaReason::no_eligible_samples,
        .counts = {.protected_samples = 512},
    };
    binary.white_samples = 384;
    reports.binarization = binary;
    require(methods::validate(reports),
            "terminal classification includes every protected photometric destination");
    methods::CompletedStages completed;
    completed.denoising = selected_stage_reports().denoising;
    require(methods::validate(completed),
            "failure completed facts retain their owned closed-family reports");
}
} // namespace docenhance::tests
