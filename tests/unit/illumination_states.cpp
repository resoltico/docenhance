// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/stages.hpp"
#include "docenhance/methods/surface.hpp"
#include "linear_fixture.hpp"
#include "protection.hpp"
#include "stub_verifier.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <utility>
namespace docenhance::tests {
namespace {
class ObservedFailure final : public app::Processor {
  public:
    ObservedFailure(methods::StageObservations stages, core::ErrorCode code)
        : stages_(std::move(stages)), code_(code) {}
    [[nodiscard]] unsigned calls() const noexcept {
        return calls_;
    }
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const core::Cancellation& /*cancellation*/) override {
        ++calls_;
        return app::process_failure({.code = code_, .message = "Observed controlled refusal"},
                                    std::move(stages_));
    }

  private:
    methods::StageObservations stages_;
    core::ErrorCode code_;
    unsigned calls_ = 0;
};
app::Invocation invocation() {
    app::Invocation request;
    request.command = app::Command::process;
    request.operands = {"input.png"};
    request.option(app::OptionId::out_dir) = "output";
    return request;
}
void check(const app::Invocation& request, methods::StageObservations stages, core::ErrorCode code,
           bool malformed) {
    UnusedVerifier verifier;
    ObservedFailure processor{std::move(stages), code};
    const auto outcome = app::dispatch(request, processor, verifier);
    const auto& error = std::get<app::Failure>(outcome.payload).error;
    CHECK(error.code == (malformed ? core::ErrorCode::publication_unknown : code));
    CHECK(error.publication ==
          (malformed ? core::Publication::unknown : core::Publication::not_started));
    CHECK(processor.calls() == 1);
}
methods::StageObservations identity(unsigned combination, const app::ProcessRequest& request) {
    constexpr image::Extent extent{.width = 32, .height = 32};
    core::Budget budget{std::size_t{1024} * 1024};
    LinearFixture source{budget, extent};
    source.fill(0.5);
    auto mask = image::Plane<std::uint8_t>::allocate(budget, extent.width, extent.height).value();
    std::ranges::fill(mask.view().storage(), 1);
    const auto& plan = std::get<app::ContinuousPlan>(request.plan()).photometric;
    auto stages = app::initial_stages(plan);
    const auto protection =
        combination == 0 ? mask.view().as_const() : image::PlaneView<const std::uint8_t>{};
    const auto model = methods::SurfaceModel::prepare({source, protection},
                                                      std::get<methods::Surface>(plan.illumination),
                                                      budget, {}, stages.illumination);
    REQUIRE(model);
    CHECK(!model->active());
    CHECK(stages.illumination.complete());
    return stages;
}
} // namespace
TEST_CASE("Surface completed identity reasons retain their actual decision priority",
          "[app][surface]") {
    constexpr unsigned combinations = 3;
    for (unsigned combination = 0; combination < combinations; ++combination) {
        auto request = invocation();
        request.option(app::OptionId::illumination) = "surface";
        request.option(app::OptionId::background_strength) = combination < 2 ? "0" : "1";
        request.option(app::OptionId::background_max_gain) = "1";
        if (combination == 0) {
            request.option(app::OptionId::protect_mask) = "mask.png";
        }
        const auto admitted = app::prepare_process(request);
        REQUIRE(admitted);
        auto stages = identity(combination, *admitted);
        check(request, stages, core::ErrorCode::resource, false);
        stages.illumination.reason = combination == 1 ? methods::IlluminationReason::unit_gain
                                                      : methods::IlluminationReason::zero_strength;
        check(request, stages, core::ErrorCode::resource, true);
    }
}
TEST_CASE("Disabled protection cancellation cannot become a failed selected illumination",
          "[app][host]") {
    core::Budget budget{std::size_t{1024} * 1024};
    constexpr std::uint32_t width = 3 * image::linear_block_pixels;
    auto mask = image::Plane<std::uint8_t>::allocate(budget, width, 1).value();
    std::ranges::fill(mask.view().row(0), 1);
    const CheckpointStop stop{core::Checkpoint::measurement, 1};
    auto request = invocation();
    request.option(app::OptionId::protect_mask) = "mask.png";
    const auto admitted = app::prepare_process(request);
    REQUIRE(admitted);
    auto stages = app::initial_stages(std::get<app::ContinuousPlan>(admitted->plan()).photometric);
    const auto counted = host::disabled_protection_observations(
        mask.view().as_const(), {.width = width, .height = 1}, stop.cancellation(),
        stages.illumination);
    REQUIRE(!counted);
    CHECK(stages.illumination.protected_samples == image::linear_block_pixels);
    CHECK(stages.illumination.progress == methods::StageProgress::preparing);
    CHECK(stages.illumination.status == methods::IlluminationStatus::disabled);
    check(request, stages, counted.error().code, false);
    stages.illumination.status = methods::IlluminationStatus::failed;
    stages.illumination.reason = methods::IlluminationReason::processing_failure;
    check(request, stages, counted.error().code, true);
}
} // namespace docenhance::tests
