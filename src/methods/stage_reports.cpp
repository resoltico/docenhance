// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/stage_reports.hpp"

#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/report_values.hpp"

#include <optional>
#include <variant>

namespace docenhance::methods {
namespace {
template <typename Facts>
bool photometric(const Facts& facts, std::optional<PixelCounts>& cohort) noexcept {
    if (!validate(facts)) {
        return false;
    }
    if (!cohort) {
        cohort = facts.counts;
        return true;
    }
    return cohort->eligible == facts.counts.eligible &&
           cohort->protected_samples == facts.counts.protected_samples;
}
template <typename... Facts>
bool photometric_family(const std::optional<std::variant<Facts...>>& report,
                        std::optional<PixelCounts>& cohort) noexcept {
    if (!report) {
        return true;
    }
    if (report->valueless_by_exception()) {
        return false;
    }
    return ((std::get_if<Facts>(&*report) != nullptr &&
             photometric(*std::get_if<Facts>(&*report), cohort)) ||
            ...);
}
template <typename Facts>
bool photometric_family(const std::optional<Facts>& report,
                        std::optional<PixelCounts>& cohort) noexcept {
    return !report || photometric(*report, cohort);
}
template <typename Facts>
bool binary(const Facts& facts, const std::optional<PixelCounts>& cohort) noexcept {
    return validate(facts) && (!cohort || facts.black_samples + facts.white_samples ==
                                              cohort->eligible + cohort->protected_samples);
}
bool binary_family(const std::optional<BinarizationFacts>& report,
                   const std::optional<PixelCounts>& cohort) noexcept {
    if (!report) {
        return true;
    }
    if (report->valueless_by_exception()) {
        return false;
    }
    const auto* const otsu = std::get_if<OtsuFacts>(&*report);
    const auto* const sauvola = std::get_if<SauvolaFacts>(&*report);
    const auto* const fixed = std::get_if<FixedFacts>(&*report);
    return (otsu != nullptr && binary(*otsu, cohort)) ||
           (sauvola != nullptr && binary(*sauvola, cohort)) ||
           (fixed != nullptr && binary(*fixed, cohort));
}
} // namespace
bool validate(const StageReports& reports) noexcept {
    std::optional<PixelCounts> cohort;
    return photometric_family(reports.illumination, cohort) &&
           photometric_family(reports.denoising, cohort) &&
           photometric_family(reports.restoration, cohort) &&
           photometric_family(reports.contrast, cohort) &&
           photometric_family(reports.sharpening, cohort) &&
           binary_family(reports.binarization, cohort);
}
bool validate(const CompletedStages& reports) noexcept {
    return validate(static_cast<const StageReports&>(reports));
}
} // namespace docenhance::methods
