// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/host/processor.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/stages.hpp"

#include <catch2/catch_test_macros.hpp>
#include <type_traits>

namespace docenhance::tests {
namespace {
contract::Invocation selected() {
    contract::Invocation v{
        .command = contract::Command::process,
        .subject = "absent-source.png",
        .output_directory = "unused-output",
    };
    v.illumination = "surface";
    v.denoise = "nlm";
    v.deblur = "wiener";
    v.psf = "gaussian";
    v.contrast = "clahe";
    v.sharpen = "unsharp";
    return v;
}
template <typename Report> void exercise_transition(const Report& initial) {
    using Status = decltype(initial.status);
    const auto request = app::prepare_process(selected());
    REQUIRE(request);
    auto stages = app::initial_stages(std::get<app::ContinuousPlan>(request->plan()).photometric);
    const auto check = [](auto& report) {
        using S = decltype(report.status);
        CHECK(report.status == S::not_started);
        report.status = S::failed;
    };
    if constexpr (std::is_same_v<Report, methods::IlluminationReport>) {
        check(stages.illumination);
    } else if constexpr (std::is_same_v<Report, methods::DenoisingReport>) {
        check(stages.denoising);
    } else if constexpr (std::is_same_v<Report, methods::RestorationReport>) {
        check(stages.restoration);
    } else if constexpr (std::is_same_v<Report, methods::ContrastReport>) {
        check(stages.contrast);
    } else {
        check(stages.sharpening);
    }
    app::failed_stages(stages);
    const auto failed = [](const auto& report) {
        using S = decltype(report.status);
        return report.status == S::failed;
    };
    CHECK(initial.status == Status::not_started);
    CHECK((failed(stages.illumination) + failed(stages.denoising) + failed(stages.restoration) +
           failed(stages.contrast) + failed(stages.sharpening)) == 1);
}
} // namespace
TEST_CASE("Unstarted selected stages are distinct from a real source acquisition failure",
          "[host]") {
    const auto request = app::prepare_process(selected());
    REQUIRE(request);
    const auto& plan = std::get<app::ContinuousPlan>(request->plan());
    const auto stages = app::initial_stages(plan.photometric);
    host::Processor processor;
    const auto result = processor.process(*request, {});
    REQUIRE(!result);
    CHECK(result.error().error.code == core::ErrorCode::input);
    CHECK(result.error().error.publication == core::Publication::not_started);
    const auto& observed = result.error().stages;
    REQUIRE(observed.has_value());
    if (observed) {
        CHECK(observed->illumination == stages.illumination);
        CHECK(observed->denoising == stages.denoising);
        CHECK(observed->restoration == stages.restoration);
        CHECK(observed->contrast == stages.contrast);
        CHECK(observed->sharpening == stages.sharpening);
    }
}
TEST_CASE("Only attempted incomplete stages fail; completed earlier facts survive", "[app]") {
    const auto request = app::prepare_process(selected());
    REQUIRE(request);
    auto stages = app::initial_stages(std::get<app::ContinuousPlan>(request->plan()).photometric);
    exercise_transition(stages.illumination);
    exercise_transition(stages.denoising);
    exercise_transition(stages.restoration);
    exercise_transition(stages.contrast);
    exercise_transition(stages.sharpening);
    stages.illumination.status = methods::IlluminationStatus::no_change;
    stages.illumination.complete = true;
    stages.illumination.reason = methods::IlluminationReason::no_eligible_samples;
    stages.illumination.protected_samples = 4;
    const auto completed = stages.illumination;
    app::failed_stages(stages);
    CHECK(stages.illumination == completed);
    CHECK(stages.restoration.status == methods::RestorationStatus::not_started);
}
} // namespace docenhance::tests
