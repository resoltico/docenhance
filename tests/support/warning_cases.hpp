// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/warnings.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/warnings.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/methods/warnings.hpp"
#include "require.hpp"
#include "restoration_fixture.hpp"
#include "stage_report_fixture.hpp"
#include "tvl1_fact_cases.hpp"

#include <algorithm>
#include <array>

namespace docenhance::tests {
inline methods::PsfFacts warning_psf_facts(std::array<double, 9> coefficients) {
    methods::normalize_psf(3, 3, coefficients).value();
    const auto centroid = methods::psf_centroid(3, 3, coefficients).value();
    double sum = 0;
    for (const auto coefficient : coefficients) {
        sum += coefficient;
    }
    return {
        .kind = methods::PsfKind::kernel,
        .width = 3,
        .height = 3,
        .normalized_sum = sum,
        .centroid_x = centroid.front(),
        .centroid_y = centroid.back(),
    };
}
inline void completed_warning_cases() {
    auto tv = completed_tv_facts();
    require(methods::validate(tv, methods::Tvl1Parameters{.iterations = 30}),
            "tolerance wins at the actual iteration cap in the warning control");
    require(!methods::warnings(tv).front(),
            "TV tolerance completion has no iteration-limit warning");
    tv.solver = methods::Tvl1Solution{
        .iterations = 10,
        .stop = methods::Tvl1Stop::iteration_limit,
        .primal_update = 0.1,
        .dual_update = 0.1,
        .objective_start = 8,
        .objective_end = 7,
    };
    require(methods::validate(tv, methods::Tvl1Parameters{.iterations = 10}),
            "the limit-warning control is a real admitted short-cap fact");
    require(methods::warnings(tv).front() == core::Warning::tv_iteration_limit,
            "TV limit warning derives from the actual completed stop");
    methods::UnsharpFacts protected_facts{.counts = {.protected_samples = 128}};
    require(methods::validate(protected_facts, methods::UnsharpParameters{}),
            "the sharpening warning control has no eligible work");
    require(methods::warnings(protected_facts).front() == core::Warning::sharpening,
            "positive sharpening remains warned even when every sample is protected");
    protected_facts.reason = methods::UnsharpReason::zero_amount;
    require(methods::validate(protected_facts, methods::UnsharpParameters{.amount = 0}),
            "the zero-amount warning control matches its selected request");
    require(!methods::warnings(protected_facts).front(),
            "zero sharpening amount has no harm warning");
    auto restoration = completed_wiener_facts();
    auto projected = methods::warnings(restoration, false);
    require(projected.front() == core::Warning::restoration_inference,
            "every enabled R01 report retains the inference warning");
    restoration.psf = edge_psf_facts(3, 3, true, true);
    projected = methods::warnings(restoration, true);
    require(std::get<1>(projected) == core::Warning::psf_after_transform &&
                std::get<2>(projected) == core::Warning::psf_off_center,
            "restoration warnings derive actual context and centroid without stored flags");
    restoration.psf = warning_psf_facts({0, 0, 0, 0, 3, 1, 0, 0, 0});
    require(methods::validate(restoration), "the exact centroid boundary is admitted");
    require(!std::get<2>(methods::warnings(restoration, false)),
            "the exact quarter-pixel centroid boundary is quiet");
    restoration.psf = warning_psf_facts({0, 0, 0, 0, 3, 1, 0, 1, 0});
    require(methods::validate(restoration), "the diagonal centroid control is admitted");
    require(std::get<2>(methods::warnings(restoration, false)) == core::Warning::psf_off_center,
            "the centroid warning uses Euclidean distance rather than independent axes");
}
inline void transform_warning_cases() {
    const methods::StageReports empty;
    const auto normal = image::Orientation::normal();
    const auto identity = image::QuarterTurn::identity();
    require(!methods::transformed_before_restoration(empty, normal, identity),
            "untransformed empty prefix does not invent PSF uncertainty");
    const auto rotated = image::QuarterTurn::from_degrees(90);
    const auto oriented = image::Orientation::from_code(6);
    if (!rotated || !oriented) {
        require(false, "the warning control uses admitted geometry");
        return;
    }
    require(methods::transformed_before_restoration(empty, normal, *rotated),
            "an applied requested rotation changes the PSF frame");
    require(methods::transformed_before_restoration(empty, *oriented, identity),
            "applied metadata orientation changes the PSF frame");
    require(methods::transformed_before_restoration(selected_stage_reports(), normal, identity),
            "actual earlier denoising writes change the assumed PSF context");
    auto unchanged_prefix = selected_stage_reports();
    unchanged_prefix.denoising = methods::NlmFacts{
        .reason = methods::NlmReason::no_effect,
        .counts = {.eligible = 512, .evaluated = 512},
        .weight_table_entries = 113337,
    };
    require(methods::validate(unchanged_prefix), "the no-effect denoising prefix is admitted");
    require(!methods::transformed_before_restoration(unchanged_prefix, normal, identity),
            "selected denoising and later contrast writes do not invent earlier changes");
}
inline void conversion_warning_cases() {
    image::InterpretationReport conversion;
    conversion.assumed_transfer = true;
    conversion.interpretation = image::Interpretation::overridden_srgb;
    conversion.flattened_pixels = 1;
    conversion.depth_reduced = true;
    const auto projected = image::warnings(conversion);
    require(std::get<0>(projected) == core::Warning::profile_assumed &&
                std::get<1>(projected) == core::Warning::profile_overridden &&
                std::get<2>(projected) == core::Warning::alpha_flattened &&
                std::get<3>(projected) == core::Warning::depth_reduced,
            "conversion warnings preserve all four existing closed identities");
    require(std::ranges::none_of(image::warnings(image::InterpretationReport{}),
                                 [](auto value) { return value.has_value(); }),
            "empty conversion observations do not fabricate warnings");
}
inline void warning_cases() {
    completed_warning_cases();
    transform_warning_cases();
    conversion_warning_cases();
}
} // namespace docenhance::tests
