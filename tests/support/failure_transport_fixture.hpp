// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/app/response.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/methods/warnings.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <stdexcept>
#include <variant>
#include <vector>
namespace docenhance::tests {
inline app::ResponseFailure failure_projection(const app::ProcessFailure& value) {
    return {
        .error = {.code = value.error.code, .message = value.error.message},
        .publication = value.publication,
        .failed_stage = value.failed_stage,
        .completed = value.completed,
        .diagnostics = value.diagnostics,
        .warnings = std::vector<core::Warning>(),
    };
}
inline void check_completed_presence(const methods::CompletedStages& actual,
                                     const methods::CompletedStages& expected) {
    CHECK(actual.illumination.has_value() == expected.illumination.has_value());
    CHECK(actual.denoising.has_value() == expected.denoising.has_value());
    CHECK(actual.restoration.has_value() == expected.restoration.has_value());
    CHECK(actual.contrast.has_value() == expected.contrast.has_value());
    CHECK(actual.sharpening.has_value() == expected.sharpening.has_value());
    CHECK(actual.binarization.has_value() == expected.binarization.has_value());
}
inline void check_failure_effects(const app::ProcessFailure& actual,
                                  const app::ProcessFailure& expected) {
    CHECK(actual.error.code == expected.error.code);
    CHECK(actual.error.message == expected.error.message);
    CHECK(actual.publication == expected.publication);
    CHECK(actual.failed_stage == expected.failed_stage);
    CHECK(actual.diagnostics.has_value() == expected.diagnostics.has_value());
}
inline void check_failure_context(const methods::FailureWarningContext& actual,
                                  const methods::FailureWarningContext& expected) {
    CHECK(actual.interpretation == expected.interpretation);
    CHECK(actual.observed_tvl1_stop == expected.observed_tvl1_stop);
    CHECK(actual.restoration_entered == expected.restoration_entered);
    CHECK(actual.restoration_after_transform == expected.restoration_after_transform);
    CHECK(actual.resolved_psf_centroid == expected.resolved_psf_centroid);
    CHECK(actual.positive_sharpening_entered == expected.positive_sharpening_entered);
}
inline void check_failure_transport(const app::ProcessFailure& actual,
                                    const app::ProcessFailure& expected) {
    check_failure_effects(actual, expected);
    check_failure_context(actual.warning_context, expected.warning_context);
    check_completed_presence(actual.completed, expected.completed);
    CHECK(methods::validate(actual.completed) == methods::validate(expected.completed));
    CHECK(actual.completed == expected.completed);
}
inline methods::SurfaceFacts& required_surface_transport(app::ProcessFailure& value) {
    REQUIRE(value.completed.illumination.has_value());
    if (!value.completed.illumination.has_value()) {
        throw std::logic_error("Required surface transport observation is absent");
    }
    return std::get<methods::SurfaceFacts>(value.completed.illumination.value());
}
inline double& required_surface_target(app::ProcessFailure& value) {
    auto& facts = required_surface_transport(value);
    REQUIRE(facts.measurements.has_value());
    if (!facts.measurements.has_value()) {
        throw std::logic_error("Required surface measurements are absent");
    }
    return facts.measurements.value().target;
}
inline methods::Tvl1Facts& required_tvl1_transport(app::ProcessFailure& value) {
    REQUIRE(value.completed.denoising.has_value());
    if (!value.completed.denoising.has_value()) {
        throw std::logic_error("Required TVL1 transport observation is absent");
    }
    return std::get<methods::Tvl1Facts>(value.completed.denoising.value());
}
inline double& required_tvl1_primal_update(app::ProcessFailure& value) {
    auto& facts = required_tvl1_transport(value);
    REQUIRE(facts.solver.has_value());
    if (!facts.solver.has_value()) {
        throw std::logic_error("Required TVL1 solver observation is absent");
    }
    return facts.solver.value().primal_update;
}
// Normalize only the intentionally non-reflexive leaf in owned comparison copies, after proving
// both actual and supplied leaves stay NaN. Every other finite-invalid or ordinary field compares.
inline void check_surface_nan_transport(const app::ProcessFailure& actual,
                                        const app::ProcessFailure& expected) {
    auto observed = actual;
    auto supplied = expected;
    auto& returned = required_surface_target(observed);
    auto& original = required_surface_target(supplied);
    CHECK(std::isnan(returned));
    CHECK(std::isnan(original));
    returned = 0;
    original = 0;
    check_failure_transport(observed, supplied);
}
inline void check_tvl1_nan_transport(const app::ProcessFailure& actual,
                                     const app::ProcessFailure& expected) {
    auto observed = actual;
    auto supplied = expected;
    auto& returned = required_tvl1_primal_update(observed);
    auto& original = required_tvl1_primal_update(supplied);
    CHECK(std::isnan(returned));
    CHECK(std::isnan(original));
    returned = 0;
    original = 0;
    check_failure_transport(observed, supplied);
}
} // namespace docenhance::tests
