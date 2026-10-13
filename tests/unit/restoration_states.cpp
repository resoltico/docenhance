// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/stages.hpp"
#include "stub_verifier.hpp"

#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <utility>
namespace docenhance::tests {
namespace {
class ReturningProcessor final : public app::Processor {
  public:
    explicit ReturningProcessor(app::ProcessResult value) : value_(std::move(value)) {}
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const core::Cancellation& /*cancellation*/) override {
        ++calls_;
        return std::move(value_);
    }
    [[nodiscard]] unsigned calls() const {
        return calls_;
    }

  private:
    app::ProcessResult value_;
    unsigned calls_ = 0;
};
contract::Invocation invocation() {
    contract::Invocation value;
    value.command = contract::Command::process;
    value.subject = "input.png";
    value.output_directory = "output";
    return value;
}
const core::Error& error(const app::Outcome& value) {
    return std::get<app::ProcessFailure>(value.payload).error;
}
} // namespace
TEST_CASE("Restoration partial failures preserve bounded observations and reject contradictions",
          "[app][restoration]") {
    UnusedVerifier verifier;
    auto request = invocation();
    request.deblur = "wiener";
    request.psf = "gaussian";
    methods::RestorationReport known{
        .status = methods::RestorationStatus::failed,
        .reason = methods::RestorationReason::processing_failure,

        .progress = methods::StageProgress::preparing,
        .inference_warning = true,
    };
    known.requested = methods::WienerParameters{};
    known.eligible_samples = 1;
    for (unsigned change = 0; change < 5; ++change) {
        auto observed = known;
        REQUIRE(observed.requested);
        if (change == 1) {
            observed.requested.value().k = 0.02;
        } else if (change == 2) {
            observed.inference_warning = false;
        } else if (change == 3) {
            observed.evaluated_samples = 1;
        } else if (change == 4) {
            observed.progress = methods::StageProgress::completed;
        }
        methods::StageObservations stages;
        stages.illumination.progress = methods::StageProgress::completed;
        stages.illumination.eligible_samples = 1;
        stages.denoising.progress = methods::StageProgress::completed;
        stages.denoising.eligible_samples = 1;
        stages.restoration = observed;
        ReturningProcessor processor{std::unexpected(app::ProcessFailure{
            .error = {.code = core::ErrorCode::resource, .message = "Restoration refused"},
            .stages = stages,
        })};
        const auto outcome = app::dispatch(request, processor, verifier);
        CHECK(processor.calls() == 1);
        CHECK(error(outcome).code ==
              (change == 0 ? core::ErrorCode::resource : core::ErrorCode::publication_unknown));
        if (change == 0) {
            const auto& failed = std::get<app::ProcessFailure>(outcome.payload);
            REQUIRE(failed.stages.has_value());
            if (failed.stages) {
                CHECK(failed.stages->restoration == observed);
            }
        }
    }
}
TEST_CASE("A later completed stage cannot follow a selected unstarted or failed predecessor",
          "[app]") {
    UnusedVerifier verifier;
    auto v = invocation();
    v.illumination = "surface";
    v.background_strength = "0";
    v.denoise = "nlm";
    v.denoise_blend = "0";
    const auto admitted = app::prepare_process(v);
    REQUIRE(admitted);
    const auto& plan = std::get<app::ContinuousPlan>(admitted->plan());
    for (unsigned state = 0; state < 3; ++state) {
        auto stages = app::initial_stages(plan.photometric);
        stages.denoising.status = methods::DenoiseStatus::no_change;
        stages.denoising.reason = methods::DenoiseReason::zero_blend;
        stages.denoising.progress = methods::StageProgress::completed;
        stages.denoising.eligible_samples = 1;
        if (state == 1) {
            stages.illumination.status = methods::IlluminationStatus::failed;
            stages.illumination.reason = methods::IlluminationReason::processing_failure;
        } else if (state == 2) {
            stages.illumination.status = methods::IlluminationStatus::no_change;
            stages.illumination.reason = methods::IlluminationReason::zero_strength;
            stages.illumination.progress = methods::StageProgress::completed;
            stages.illumination.eligible_samples = 1;
        }
        ReturningProcessor processor{app::process_failure(
            {.code = core::ErrorCode::resource, .message = "Later allocation refused"},
            std::move(stages))};
        const auto outcome = app::dispatch(v, processor, verifier);
        CHECK(error(outcome).code ==
              (state == 2 ? core::ErrorCode::resource : core::ErrorCode::publication_unknown));
    }
}
TEST_CASE("Early continuous input failure retains known state with sharpening off", "[app]") {
    UnusedVerifier verifier;
    const methods::StageObservations unreported;
    ReturningProcessor processor{app::process_failure(
        {.code = core::ErrorCode::input, .message = "Input rejected before processing"},
        unreported)};
    const auto outcome = app::dispatch(invocation(), processor, verifier);
    CHECK(processor.calls() == 1);
    CHECK(error(outcome).code == core::ErrorCode::input);
    CHECK(error(outcome).publication == core::Publication::not_started);
}
} // namespace docenhance::tests
