// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/report/warnings.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/core/warnings.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/warnings.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/methods/warnings.hpp"
#include "docenhance/record/record.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string_view>
#include <variant>

namespace docenhance::report {
namespace {
void insert(RunWarnings& output, core::Warning value) noexcept {
    constexpr auto names = core::EnumNames<core::Warning>::list;
    const auto position = std::ranges::distance(
        names.begin(),
        std::ranges::find_if(names, [value](const auto& name) { return name.first == value; }));
    if (position == std::ranges::distance(names)) {
        return;
    }
    *std::ranges::next(output.begin(), position) = value;
}
void merge(RunWarnings& output, const core::WarningList& projection) noexcept {
    for (const auto warning : projection) {
        if (warning) {
            insert(output, *warning);
        }
    }
}
template <std::size_t Index = 0, typename Facts>
void project_variant(RunWarnings& output, const Facts& facts) noexcept {
    if constexpr (Index < std::variant_size_v<Facts>) {
        if (const auto* const value = std::get_if<Index>(&facts)) {
            merge(output, methods::warnings(*value));
        } else {
            project_variant<Index + 1>(output, facts);
        }
    }
}
template <typename Facts>
void project(RunWarnings& output, const std::optional<Facts>& facts) noexcept {
    if (!facts) {
        return;
    }
    if constexpr (requires { facts->valueless_by_exception(); }) {
        project_variant(output, *facts);
    } else {
        merge(output, methods::warnings(*facts));
    }
}
void completed(RunWarnings& output, const methods::StageReports& reports,
               const std::optional<image::InterpretationReport>& interpretation) noexcept {
    if (interpretation) {
        merge(output, image::warnings(*interpretation));
    }
    project(output, reports.illumination);
    project(output, reports.denoising);
    if (reports.restoration) {
        const bool after =
            interpretation && methods::transformed_before_restoration(
                                  reports, interpretation->orientation, interpretation->rotation);
        merge(output, methods::warnings(*reports.restoration, after));
    }
    project(output, reports.contrast);
    project(output, reports.sharpening);
    project(output, reports.binarization);
}
} // namespace
RunWarnings run_warnings(const record::RunRecord& value) noexcept {
    RunWarnings result{};
    completed(result, value.execution, value.execution.interpretation);
    return result;
}
RunWarnings failure_warnings(const app::ProcessFailure& value) noexcept {
    RunWarnings result{};
    const auto& context = value.warning_context;
    completed(result, value.completed, context.interpretation);
    if (context.observed_tvl1_stop == methods::Tvl1Stop::iteration_limit) {
        insert(result, core::Warning::tv_iteration_limit);
    }
    if (context.restoration_entered) {
        insert(result, core::Warning::restoration_inference);
        if (context.restoration_after_transform) {
            insert(result, core::Warning::psf_after_transform);
        }
        if (context.resolved_psf_centroid &&
            std::hypot(context.resolved_psf_centroid->front(),
                       context.resolved_psf_centroid->back()) > methods::psf_off_center_limit) {
            insert(result, core::Warning::psf_off_center);
        }
    }
    if (context.positive_sharpening_entered) {
        insert(result, core::Warning::sharpening);
    }
    return result;
}
std::optional<std::string_view> warning_explanation(core::Warning warning) noexcept {
    switch (warning) {
    case core::Warning::profile_assumed:
        return "Color interpretation assumes sRGB where declarations are absent; check that the "
               "transfer and primaries assumptions suit the source.";
    case core::Warning::profile_overridden:
        return "The selected sRGB policy overrides color declarations; colors may change.";
    case core::Warning::alpha_flattened:
        return "Non-opaque pixels were composited in linear light. Output is opaque; original "
               "alpha cannot be recovered.";
    case core::Warning::depth_reduced:
        return "Decoded samples were reduced in precision by the output request; discarded "
               "precision cannot be recovered.";
    case core::Warning::tv_iteration_limit:
        return "TV-L1 reached its iteration cap without satisfying its stopping rule. "
               "This is not a convergence certificate.";
    case core::Warning::restoration_inference:
        return "Known-PSF restoration is an inference; the actual camera blur and original "
               "document meaning are not verified.";
    case core::Warning::psf_after_transform:
        return "Orientation, rotation, or changed illumination/denoising precedes restoration; the "
               "assumed spatially invariant PSF may be approximate.";
    case core::Warning::psf_off_center:
        return "The PSF centroid is more than 0.25 pixels from its center; coefficients were not "
               "recentered.";
    case core::Warning::sharpening:
        return "Sharpening can alter faint marks or introduce halos. Inspect the output and the "
               "reported pre-clamp excursions.";
    }
    return std::nullopt;
}
} // namespace docenhance::report
