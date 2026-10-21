// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/response.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/methods/tvl1_facts.hpp"
#include "docenhance/pipeline/render.hpp"
#include "failure_transport_fixture.hpp"
#include "png_fixture.hpp"
#include "stub_verifier.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
namespace docenhance::tests {
namespace {
class LifecycleProcessor final : public app::Processor {
  public:
    explicit LifecycleProcessor(app::ProcessFailure failure) : failure_(std::move(failure)) {}
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const app::ExecutionControl& /*control*/) override {
        ++calls_;
        return std::unexpected(failure_);
    }
    [[nodiscard]] unsigned calls() const noexcept {
        return calls_;
    }

  private:
    unsigned calls_ = 0;
    app::ProcessFailure failure_;
};
app::Invocation lifecycle_request() {
    app::Invocation request;
    request.command = app::Command::process;
    request.operands = {"input.png"};
    request.option(app::OptionId::out_dir) = "output";
    request.option(app::OptionId::illumination) = "surface";
    request.option(app::OptionId::background_cell) = "8";
    request.option(app::OptionId::background_target) = "1";
    request.option(app::OptionId::denoise) = "nlm";
    return request;
}
pipeline::RenderResult lifecycle_render(core::Budget& budget, const methods::Plan& plan,
                                        unsigned side = 0) {
    std::vector<std::uint8_t> samples(1024);
    for (std::uint32_t y = 0; y < 32; ++y) {
        for (std::uint32_t x = 0; x < 32; ++x) {
            samples.at((y * 32) + x) = static_cast<std::uint8_t>(80 + ((x + (3 * y)) % 20));
        }
    }
    const auto png = make_gray_png({
        .width = 32,
        .height = 32,
        .depth = 8,
        .interlaced = false,
        .samples = std::move(samples),
    });
    pipeline::Input input;
    input.encoded().source = budget.allocate(png.size()).value();
    std::ranges::copy(std::as_bytes(std::span{png}), input.encoded().source.bytes().begin());
    input.encoded().source_identity = fs::identify(input.encoded().source.bytes()).value();
    if (side != 0) {
        std::vector<std::uint8_t> protected_samples(1024, 1);
        for (unsigned y = 0; y < side; ++y) {
            for (unsigned x = 0; x < side; ++x) {
                protected_samples.at((y * 32) + x) = 0;
            }
        }
        const auto mask = make_gray_png({
            .width = 32,
            .height = 32,
            .depth = 1,
            .interlaced = false,
            .samples = std::move(protected_samples),
        });
        input.encoded().mask = budget.allocate(mask.size()).value();
        std::ranges::copy(std::as_bytes(std::span{mask}), input.encoded().mask.bytes().begin());
        input.encoded().mask_identity = fs::identify(input.encoded().mask.bytes()).value();
    }
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    return pipeline::render(std::move(input), plan, budget, scheduler);
}
app::ProcessFailure after_surface(const pipeline::Rendering& rendered) {
    app::ProcessFailure failure{
        .error = {.code = core::ErrorCode::numerical, .message = "Observed source failure"},
        .failed_stage = methods::Family::denoising,
    };
    failure.completed.illumination = rendered.stages.illumination;
    failure.warning_context.interpretation = rendered.interpretation;
    return failure;
}
void mutate_surface_facts(methods::SurfaceFacts& facts, unsigned mutation) {
    if (mutation == 1) {
        facts.disposition = methods::Disposition::no_change;
    }
    if (mutation == 2) {
        facts.reason = methods::SurfaceReason::zero_strength;
    }
    if (mutation == 3) {
        facts.solver.reset();
    }
    if (mutation == 4) {
        REQUIRE(facts.measurements.has_value());
        if (!facts.measurements.has_value()) {
            throw std::logic_error("Required test observation is absent");
        }
        facts.measurements.value().target = std::numeric_limits<double>::quiet_NaN();
    }
    if (mutation == 5) {
        --facts.counts.evaluated;
    }
    if (mutation == 6) {
        REQUIRE(facts.solver.has_value());
        if (!facts.solver.has_value()) {
            throw std::logic_error("Required test observation is absent");
        }
        facts.solver.value().residual = facts.solver.value().tolerance + 1;
    }
}
void mutate_tvl1_facts(methods::Tvl1Facts& facts, unsigned mutation) {
    REQUIRE(facts.solver.has_value());
    if (!facts.solver.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    if (mutation == 1) {
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        facts.solver.value().stop = static_cast<methods::Tvl1Stop>(99);
    }
    if (mutation == 2) {
        facts.solver.value().passing_checkpoints = 3;
    }
    if (mutation == 3) {
        facts.solver.value().primal_update = std::numeric_limits<double>::quiet_NaN();
    }
    if (mutation == 4) {
        facts.solver.value().dual_update = 3;
    }
    if (mutation == 5) {
        facts.solver.value().objective_start = -1;
    }
    if (mutation == 6) {
        facts.solver.value().iterations = 0;
    }
    if (mutation == 7) {
        --facts.counts.evaluated;
    }
}
} // namespace
TEST_CASE(
    "Actual Surface validators refuse malformed Facts while dispatch retains reported effects") {
    const auto request = lifecycle_request();
    const auto admitted = app::prepare_process(request);
    REQUIRE(admitted);
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    {
        auto plan = admitted->plan();
        plan.photometric.denoising = methods::DenoisingOff{};
        const auto rendered = lifecycle_render(budget, plan);
        REQUIRE(rendered);
        REQUIRE(rendered->stages.illumination.has_value());
        const auto healthy = after_surface(*rendered);
        UnusedVerifier verifier;
        for (unsigned mutation = 0; mutation < 7; ++mutation) {
            auto failure = healthy;
            if (!failure.completed.illumination.has_value()) {
                throw std::logic_error("Required copied observation is absent");
            }
            auto& facts = std::get<methods::SurfaceFacts>(failure.completed.illumination.value());
            mutate_surface_facts(facts, mutation);
            CHECK(methods::validate(facts) == (mutation == 0));
            LifecycleProcessor processor{failure};
            const auto outcome = app::dispatch(request, processor, verifier);
            const auto& actual = std::get<app::Failure>(outcome.payload);
            if (mutation == 4) {
                check_surface_nan_transport(actual, failure);
            } else {
                check_failure_transport(actual, failure);
            }
            CHECK(app::validate(failure_projection(failure)) == (mutation == 0));
            CHECK(processor.calls() == 1);
        }
    }
    CHECK(budget.used() == 0);
}
TEST_CASE(
    "Wire suffix ordering owns rejection while dispatch transports the reported sparse prefix") {
    const auto request = lifecycle_request();
    const auto admitted = app::prepare_process(request);
    REQUIRE(admitted);
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    {
        auto plan = admitted->plan();
        plan.photometric.denoising = methods::DenoisingOff{};
        const auto rendered = lifecycle_render(budget, plan);
        REQUIRE(rendered);
        UnusedVerifier verifier;
        for (unsigned mutation = 0; mutation < 5; ++mutation) {
            auto invocation = request;
            auto failure = after_surface(*rendered);
            if (mutation == 1) {
                failure.failed_stage = methods::Family::illumination;
            }
            if (mutation == 2) {
                failure.completed.illumination.reset();
            }
            if (mutation == 3) {
                invocation.option(app::OptionId::illumination) = "off";
                invocation.option(app::OptionId::background_cell).reset();
                invocation.option(app::OptionId::background_target).reset();
            }
            if (mutation == 4) {
                failure.publication = core::Publication::completed;
            }
            REQUIRE(app::prepare_process(invocation));
            LifecycleProcessor processor{failure};
            const auto outcome = app::dispatch(invocation, processor, verifier);
            const auto& actual = std::get<app::Failure>(outcome.payload);
            check_failure_transport(actual, failure);
            CHECK(app::validate(failure_projection(failure)) == (mutation != 1 && mutation != 4));
            CHECK(processor.calls() == 1);
        }
    }
    CHECK(budget.used() == 0);
}
TEST_CASE(
    "Real explicit I01 applicability failures and automatic skips preserve actual stage truth") {
    for (const unsigned side : {1U, 8U}) {
        core::Budget budget{std::size_t{16} * 1024 * 1024};
        {
            methods::Plan plan;
            plan.protection = true;
            plan.photometric.illumination = methods::Surface::create({.cell = 8}).value();
            const auto failed = lifecycle_render(budget, plan, side);
            REQUIRE(!failed);
            CHECK(failed.error().error.code == core::ErrorCode::method_inapplicable);
            CHECK(failed.error().error.message == "Insufficient eligible I01 measurements");
            CHECK(failed.error().failed_stage == methods::Family::illumination);
            CHECK(!failed.error().completed.illumination);
            CHECK(!failed.error().completed.denoising);
            CHECK(!failed.error().completed.restoration);
            CHECK(!failed.error().completed.contrast);
            CHECK(!failed.error().completed.sharpening);
            CHECK(!failed.error().completed.binarization);
            CHECK(budget.used() == 0);
            plan.photometric.illumination =
                methods::Surface::create({.mode = methods::SurfaceMode::automatic, .cell = 8})
                    .value();
            const auto skipped = lifecycle_render(budget, plan, side);
            REQUIRE(skipped);
            REQUIRE(skipped->stages.illumination.has_value());
            if (!skipped->stages.illumination.has_value()) {
                throw std::logic_error("Required test observation is absent");
            }
            const auto& facts =
                std::get<methods::SurfaceFacts>(skipped->stages.illumination.value());
            CHECK(facts.disposition == methods::Disposition::skipped);
            CHECK(facts.reason == (side == 1 ? methods::SurfaceReason::insufficient_samples
                                             : methods::SurfaceReason::insufficient_cells));
            CHECK(facts.counts.eligible == std::uint64_t{side} * side);
            CHECK(facts.counts.evaluated == 0);
            CHECK(facts.counts.changed == 0);
            CHECK(skipped->output.decoded_and_compared);
        }
        CHECK(budget.used() == 0);
    }
}
TEST_CASE("Actual TVL1 and wire validators reject fictional stopping and partial solver values") {
    auto request = lifecycle_request();
    request.option(app::OptionId::illumination) = "off";
    request.option(app::OptionId::background_cell).reset();
    request.option(app::OptionId::background_target).reset();
    request.option(app::OptionId::denoise) = "tvl1";
    const auto admitted = app::prepare_process(request);
    REQUIRE(admitted);
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    {
        const auto rendered = lifecycle_render(budget, admitted->plan());
        REQUIRE(rendered);
        REQUIRE(rendered->stages.denoising.has_value());
        UnusedVerifier verifier;
        for (unsigned mutation = 0; mutation < 8; ++mutation) {
            app::ProcessFailure failure{
                .error =
                    {
                        .code = core::ErrorCode::numerical,
                        .message = "After completed denoising",
                    },
            };
            failure.completed.denoising = rendered->stages.denoising;
            failure.warning_context.interpretation = rendered->interpretation;
            if (!failure.completed.denoising.has_value()) {
                throw std::logic_error("Required copied observation is absent");
            }
            auto& facts = std::get<methods::Tvl1Facts>(failure.completed.denoising.value());
            mutate_tvl1_facts(facts, mutation);
            CHECK(methods::validate(facts) == (mutation == 0));
            LifecycleProcessor processor{failure};
            const auto outcome = app::dispatch(request, processor, verifier);
            const auto& actual = std::get<app::Failure>(outcome.payload);
            if (mutation == 3) {
                check_tvl1_nan_transport(actual, failure);
            } else {
                check_failure_transport(actual, failure);
            }
            CHECK(app::validate(failure_projection(failure)) == (mutation == 0));
            CHECK(processor.calls() == 1);
        }
    }
    CHECK(budget.used() == 0);
}
TEST_CASE("Wire failed contrast excludes later completed sharpening while dispatch retains "
          "observations") {
    auto request = lifecycle_request();
    request.option(app::OptionId::illumination) = "off";
    request.option(app::OptionId::background_cell).reset();
    request.option(app::OptionId::background_target).reset();
    request.option(app::OptionId::denoise) = "off";
    request.option(app::OptionId::contrast) = "gamma";
    request.option(app::OptionId::gamma) = "2";
    request.option(app::OptionId::sharpen) = "unsharp";
    request.option(app::OptionId::sharpen_amount) = "0";
    const auto admitted = app::prepare_process(request);
    REQUIRE(admitted);
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    {
        const auto rendered = lifecycle_render(budget, admitted->plan());
        REQUIRE(rendered);
        REQUIRE(rendered->stages.contrast.has_value());
        REQUIRE(rendered->stages.sharpening.has_value());
        UnusedVerifier verifier;
        for (const bool later_completed : {false, true}) {
            app::ProcessFailure failure{
                .error = {.code = core::ErrorCode::numerical, .message = "Contrast failed"},
                .failed_stage = methods::Family::contrast,
            };
            failure.warning_context.interpretation = rendered->interpretation;
            if (later_completed) {
                failure.completed.sharpening = rendered->stages.sharpening;
            }
            LifecycleProcessor processor{failure};
            const auto outcome = app::dispatch(request, processor, verifier);
            const auto& actual = std::get<app::Failure>(outcome.payload);
            check_failure_transport(actual, failure);
            CHECK(app::validate(failure_projection(failure)) == !later_completed);
            CHECK(processor.calls() == 1);
        }
    }
    CHECK(budget.used() == 0);
}
} // namespace docenhance::tests
