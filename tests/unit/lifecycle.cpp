// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/stages.hpp"
#include "docenhance/methods/surface.hpp"
#include "linear_fixture.hpp"
#include "protection.hpp"
#include "stub_verifier.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
namespace docenhance::tests {
namespace {
class LifecycleProcessor final : public app::Processor {
  public:
    explicit LifecycleProcessor(methods::StageObservations observations,
                                core::ErrorCode code = core::ErrorCode::numerical)
        : observations_(std::move(observations)), code_(code) {}
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const core::Cancellation& /*cancellation*/) override {
        ++calls_;
        return app::process_failure({.code = code_, .message = "Observed source failure"},
                                    observations_);
    }
    [[nodiscard]] unsigned calls() const noexcept {
        return calls_;
    }

  private:
    methods::StageObservations observations_;
    unsigned calls_ = 0;
    core::ErrorCode code_;
};
contract::Invocation lifecycle_request() {
    contract::Invocation request;
    request.command = contract::Command::process;
    request.subject = "input.png";
    request.output_directory = "output";
    request.illumination = "surface";
    request.denoise = "nlm";
    return request;
}
methods::IlluminationReport fitted_surface() {
    methods::IlluminationReport report;
    report.requested = methods::SurfaceParameters{};
    report.progress = methods::StageProgress::completed;
    report.status = methods::IlluminationStatus::applied;
    report.cell = 8;
    report.cells = 16;
    report.measured_cells = 16;
    report.background_reference = 0.5;
    report.predicates = {true, false, false, false, false, false};
    report.eligible_samples = 1024;
    report.evaluated_samples = 1024;
    report.changed_samples = 1;
    report.solver = methods::SolverReport{.tolerance = 0.001};
    report.measurements = methods::SurfaceMeasurements{.stride = 1, .count = 1024, .target = 1};
    return report;
}
} // namespace
TEST_CASE("Completed surface semantic checks are shared by success and failure admission",
          "[app][surface]") {
    const auto request = lifecycle_request();
    const auto admitted = app::prepare_process(request);
    REQUIRE(admitted);
    const auto initial =
        app::initial_stages(std::get<app::ContinuousPlan>(admitted->plan()).photometric);
    UnusedVerifier verifier;
    for (unsigned mutation = 0; mutation < 6; ++mutation) {
        auto stages = initial;
        stages.illumination = fitted_surface();
        stages.denoising.progress = methods::StageProgress::preparing;
        stages.denoising.status = methods::DenoiseStatus::failed;
        stages.denoising.reason = methods::DenoiseReason::processing_failure;
        stages.denoising.eligible_samples = 1024;
        auto& r = stages.illumination;
        if (mutation == 1) {
            r.status = methods::IlluminationStatus::disabled;
        }
        if (mutation == 2) {
            r.reason = methods::IlluminationReason::zero_strength;
        }
        if (mutation == 3) {
            r.solver.reset();
        }
        if (mutation == 4) {
            REQUIRE(r.measurements.has_value());
            if (r.measurements) {
                r.measurements->target = std::numeric_limits<double>::quiet_NaN();
            }
        }
        if (mutation == 5) {
            --r.evaluated_samples;
        }
        CHECK(methods::valid_illumination_observations(r) == (mutation == 0));
        LifecycleProcessor processor{stages};
        const auto outcome = app::dispatch(request, processor, verifier);
        const auto& failure = std::get<app::Failure>(outcome.payload);
        CHECK(failure.error.code ==
              (mutation == 0 ? core::ErrorCode::numerical : core::ErrorCode::publication_unknown));
        CHECK(processor.calls() == 1);
    }
}
TEST_CASE("Delegated source failure preserves the preparing consumer and rejects invented models",
          "[app]") {
    const auto request = lifecycle_request();
    const auto admitted = app::prepare_process(request);
    REQUIRE(admitted);
    const auto initial =
        app::initial_stages(std::get<app::ContinuousPlan>(admitted->plan()).photometric);
    UnusedVerifier verifier;
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
        auto stages = initial;
        stages.illumination = fitted_surface();
        auto& i = stages.illumination;
        i.progress = methods::StageProgress::evaluating;
        i.status = methods::IlluminationStatus::failed;
        i.reason = methods::IlluminationReason::processing_failure;
        i.evaluated_samples = 3;
        stages.denoising.progress = methods::StageProgress::preparing;
        stages.denoising.eligible_samples = 1024;
        if (mutation == 1) {
            stages.denoising.status = methods::DenoiseStatus::failed;
        }
        if (mutation == 2) {
            stages.denoising.progress = methods::StageProgress::prepared;
        }
        if (mutation == 3) {
            stages.restoration.progress = methods::StageProgress::completed;
        }
        LifecycleProcessor processor{stages};
        const auto outcome = app::dispatch(request, processor, verifier);
        const auto& failure = std::get<app::Failure>(outcome.payload);
        CHECK(failure.error.code ==
              (mutation == 0 ? core::ErrorCode::numerical : core::ErrorCode::publication_unknown));
        CHECK(processor.calls() == 1);
    }
}
TEST_CASE("Unconverged solver facts remain legitimate failed preparation", "[surface]") {
    auto report = fitted_surface();
    report.progress = methods::StageProgress::preparing;
    report.status = methods::IlluminationStatus::failed;
    report.reason = methods::IlluminationReason::processing_failure;
    report.evaluated_samples = 0;
    report.changed_samples = 0;
    report.measurements.reset();
    REQUIRE(report.solver.has_value());
    if (report.solver) {
        report.solver->residual = 1;
    }
    CHECK(methods::valid_illumination_observations(report));
    report.progress = methods::StageProgress::prepared;
    report.status = methods::IlluminationStatus::unresolved;
    report.reason = methods::IlluminationReason::none;
    CHECK(!methods::valid_illumination_observations(report));
}
TEST_CASE("Disabled protection counting stops within a wide row without allocating", "[host]") {
    core::Budget budget{std::size_t{1024} * 1024};
    auto mask = image::Plane<std::uint8_t>::allocate(budget, 3 * image::linear_block_pixels, 1);
    REQUIRE(mask);
    std::ranges::fill(mask->view().row(0), 1);
    const auto held = budget.used();
    const CheckpointStop stop{core::Checkpoint::measurement, 1};
    methods::IlluminationReport report;
    auto observed = host::disabled_protection_observations(mask->view().as_const(),
                                                           {.width = mask->width(), .height = 1},
                                                           stop.cancellation(), report);
    REQUIRE(!observed);
    CHECK(observed.error().code == core::ErrorCode::cancelled);
    CHECK(report.progress == methods::StageProgress::preparing);
    CHECK(report.protected_samples == image::linear_block_pixels);
    CHECK(budget.used() == held);
    REQUIRE(host::disabled_protection_observations(
        mask->view().as_const(), {.width = mask->width(), .height = 1}, {}, report));
    CHECK(report.complete());
    CHECK(report.protected_samples == mask->width());
    CHECK(report.eligible_samples == 0);
    CHECK(budget.used() == held);
}

TEST_CASE("Prepared gamma cannot be followed by a completed sharpening identity", "[app]") {
    auto request = lifecycle_request();
    request.illumination = "off";
    request.denoise = "off";
    request.contrast = "gamma";
    request.gamma = "2";
    request.sharpen = "unsharp";
    request.sharpen_amount = "0";
    const auto admitted = app::prepare_process(request);
    REQUIRE(admitted);
    const auto& plan = std::get<app::ContinuousPlan>(admitted->plan());
    UnusedVerifier verifier;
    for (const bool later_started : {false, true}) {
        auto stages = app::initial_stages(plan.photometric);
        stages.illumination.progress = methods::StageProgress::completed;
        stages.illumination.eligible_samples = 16;
        stages.denoising.eligible_samples = 16;
        stages.restoration.eligible_samples = 16;
        stages.denoising.progress = methods::StageProgress::completed;
        stages.restoration.progress = methods::StageProgress::completed;
        stages.contrast.progress = methods::StageProgress::prepared;
        stages.contrast.eligible_samples = 16;
        if (later_started) {
            stages.sharpening.progress = methods::StageProgress::completed;
            stages.sharpening.status = methods::SharpenStatus::no_change;
            stages.sharpening.reason = methods::SharpenReason::zero_amount;
            stages.sharpening.eligible_samples = 16;
        }
        LifecycleProcessor processor{stages};
        const auto result = app::dispatch(request, processor, verifier);
        CHECK(std::get<app::Failure>(result.payload).error.code ==
              (later_started ? core::ErrorCode::publication_unknown : core::ErrorCode::numerical));
        CHECK(processor.calls() == 1);
    }
}
TEST_CASE("Disabled predecessor entry and counted masks remain truthful on partial returns",
          "[app]") {
    contract::Invocation request;
    request.command = contract::Command::process;
    request.subject = "input.png";
    request.output_directory = "output";
    request.denoise = "nlm";
    request.denoise_blend = "0";
    const auto admitted = app::prepare_process(request);
    REQUIRE(admitted);
    UnusedVerifier verifier;
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
        auto stages =
            app::initial_stages(std::get<app::ContinuousPlan>(admitted->plan()).photometric);
        stages.illumination.progress = methods::StageProgress::completed;
        stages.illumination.eligible_samples = 16;
        stages.denoising.progress = methods::StageProgress::completed;
        stages.denoising.status = methods::DenoiseStatus::no_change;
        stages.denoising.reason = methods::DenoiseReason::zero_blend;
        stages.denoising.eligible_samples = 16;
        if (mutation == 1) {
            stages.illumination = {};
        }
        if (mutation == 2) {
            stages.illumination.progress = methods::StageProgress::preparing;
        }
        if (mutation == 3) {
            --stages.denoising.eligible_samples;
        }
        LifecycleProcessor processor{stages};
        const auto result = app::dispatch(request, processor, verifier);
        CHECK(std::get<app::Failure>(result.payload).error.code ==
              (mutation == 0 ? core::ErrorCode::numerical : core::ErrorCode::publication_unknown));
        CHECK(processor.calls() == 1);
    }
}

TEST_CASE("TV-L1 failure reports reject native claims and fictional stopping", "[app][tvl1]") {
    UnusedVerifier verifier;
    auto request = lifecycle_request();
    request.illumination = "off";
    request.denoise = "tvl1";
    const methods::Tvl1Parameters parameters;
    for (unsigned mutation = 0; mutation < 8; ++mutation) {
        methods::DenoisingReport report{
            .status = methods::DenoiseStatus::failed,
            .reason = methods::DenoiseReason::processing_failure,

            .progress = methods::StageProgress::preparing,
            .requested = parameters,
        };
        if (mutation == 1) {
            report.native_calls = 1;
        }
        if (mutation == 2) {
            report.tvl1 = methods::Tvl1Report{
                .iterations = 20,
                .stop = methods::Tvl1Stop::tolerance_met,
                .passing_checkpoints = 2,
            };
        }
        if (mutation == 3) {
            report.tvl1 = methods::Tvl1Report{.primal_update = 1};
        }
        if (mutation >= 4) {
            report.eligible_samples = 1;
            report.preparation_charge_peak = mutation == 7 ? 320 : 100000;
            report.tvl1 = methods::Tvl1Report{
                .iterations = 20,
                .passing_checkpoints = mutation == 5 ? 0U : 1U,
                .dual_update = mutation == 4 ? 1.0 : 0.0,
                .objective_start = 0,
                .field_bytes = 64,
            };
        }
        methods::StageObservations stages{.denoising = report};
        stages.illumination.progress = methods::StageProgress::completed;
        stages.illumination.eligible_samples = 1;
        LifecycleProcessor processor{stages};
        const auto result =
            std::get<app::Failure>(app::dispatch(request, processor, verifier).payload).error;
        CHECK(result.publication == ((mutation == 0 || mutation == 6)
                                         ? core::Publication::not_started
                                         : core::Publication::unknown));
    }
}

TEST_CASE("Real explicit Surface applicability refusals retain their specific failed reasons",
          "[app][surface]") {
    constexpr image::Extent extent{.width = 32, .height = 32};
    core::Budget budget{std::size_t{1024} * 1024};
    LinearFixture source{budget, extent};
    source.fill(0.5);
    auto mask = image::Plane<std::uint8_t>::allocate(budget, extent.width, extent.height).value();
    auto invocation = lifecycle_request();
    invocation.denoise = "off";
    invocation.background_cell = "8";
    invocation.protect_mask = "mask.png";
    const auto request = app::prepare_process(invocation);
    REQUIRE(request);
    const auto& plan = std::get<app::ContinuousPlan>(request->plan()).photometric;
    UnusedVerifier verifier;
    for (const unsigned side : {1U, 8U}) {
        std::ranges::fill(mask.view().storage(), 1);
        for (unsigned y = 0; y < side; ++y) {
            std::ranges::fill(mask.view().row(y).first(side), 0);
        }
        auto stages = app::initial_stages(plan);
        auto model = methods::SurfaceModel::prepare({source, mask.view().as_const()},
                                                    std::get<methods::Surface>(plan.illumination),
                                                    budget, {}, stages.illumination);
        REQUIRE(!model);
        CHECK(model.error().code == core::ErrorCode::method_inapplicable);
        CHECK(stages.illumination.reason ==
              (side == 1 ? methods::IlluminationReason::insufficient_samples
                         : methods::IlluminationReason::insufficient_cells));
        LifecycleProcessor processor{stages, model.error().code};
        const auto result = app::dispatch(invocation, processor, verifier);
        CHECK(std::get<app::Failure>(result.payload).error.code ==
              core::ErrorCode::method_inapplicable);
        CHECK(processor.calls() == 1);
    }
}
} // namespace docenhance::tests
