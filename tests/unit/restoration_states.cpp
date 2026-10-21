// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/response.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/pipeline/render.hpp"
#include "failure_transport_fixture.hpp"
#include "png_fixture.hpp"
#include "stub_verifier.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>
namespace docenhance::tests {
namespace {
class ReturningProcessor final : public app::Processor {
  public:
    explicit ReturningProcessor(app::ProcessFailure value) : value_(std::move(value)) {}
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const app::ExecutionControl& /*control*/) override {
        ++calls_;
        return std::unexpected(std::move(value_));
    }
    [[nodiscard]] unsigned calls() const noexcept {
        return calls_;
    }

  private:
    unsigned calls_ = 0;
    app::ProcessFailure value_;
};
app::Invocation invocation() {
    app::Invocation value;
    value.command = app::Command::process;
    value.operands = {"input.png"};
    value.option(app::OptionId::out_dir) = "output";
    value.option(app::OptionId::illumination) = "surface";
    value.option(app::OptionId::background_strength) = "0";
    value.option(app::OptionId::denoise) = "nlm";
    value.option(app::OptionId::deblur) = "wiener";
    value.option(app::OptionId::psf) = "kernel";
    value.option(app::OptionId::psf_file) = "kernel.png";
    value.option(app::OptionId::contrast) = "gamma";
    value.option(app::OptionId::sharpen) = "unsharp";
    return value;
}
pipeline::Input restoration_input(core::Budget& budget, image::Extent extent,
                                  std::span<const std::uint8_t> kernel) {
    std::vector<std::uint8_t> samples(std::size_t{extent.width} * extent.height);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples.at(index) = static_cast<std::uint8_t>((37 * index) % 256);
    }
    const auto bytes = make_gray_png({
        .width = extent.width,
        .height = extent.height,
        .depth = 8,
        .interlaced = false,
        .samples = samples,
    });
    auto source = budget.allocate(bytes.size());
    auto psf = budget.allocate(kernel.size());
    REQUIRE(source);
    REQUIRE(psf);
    std::ranges::copy(std::as_bytes(std::span{bytes}), source->bytes().begin());
    std::ranges::copy(std::as_bytes(kernel), psf->bytes().begin());
    const auto identity = fs::identify(source->bytes());
    const auto psf_identity = fs::identify(psf->bytes());
    REQUIRE(identity);
    if (!identity.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    REQUIRE(psf_identity);
    pipeline::Input input;
    input.encoded().source = std::move(*source);
    input.encoded().source_identity = *identity;
    input.encoded().psf = std::move(*psf);
    input.encoded().psf_identity = *psf_identity;
    return input;
}
void check_unstarted_restoration_suffix(const methods::CompletedStages& completed) {
    CHECK(!completed.restoration);
    CHECK(!completed.contrast);
    CHECK(!completed.sharpening);
    CHECK(!completed.binarization);
}
void check_restoration_prefix(const pipeline::Failure& failure) {
    REQUIRE(failure.completed.illumination.has_value());
    REQUIRE(failure.completed.denoising.has_value());
    if (!failure.completed.denoising.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    const auto& nlm = std::get<methods::NlmFacts>(failure.completed.denoising.value());
    CHECK(nlm.counts.evaluated == std::uint64_t{21} * 21);
    CHECK(nlm.weight_table_entries != 0);
    check_unstarted_restoration_suffix(failure.completed);
}
app::ProcessFailure failed_restoration(const app::Invocation& raw, core::Budget& budget) {
    const auto request = app::prepare_process(raw);
    REQUIRE(request);
    const std::array<std::uint8_t, 1> malformed{'x'};
    auto input = restoration_input(budget, {.width = 21, .height = 21}, malformed);
    const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
    REQUIRE(concurrency);
    const exec::Scheduler scheduler{*concurrency};
    auto result = pipeline::render(std::move(input), request->plan(), budget, scheduler);
    REQUIRE(!result);
    CHECK(result.error().error.code == core::ErrorCode::input);
    CHECK(result.error().failed_stage == methods::Family::restoration);
    check_restoration_prefix(result.error());
    CHECK(budget.used() == 0);
    return {
        .error = std::move(result.error().error),
        .publication = core::Publication::not_started,
        .failed_stage = result.error().failed_stage,
        .completed = result.error().completed,
        .warning_context = result.error().warning_context,
    };
}
void configure_kernel_bypass(methods::Plan& plan) {
    const auto turn = image::QuarterTurn::from_degrees(90);
    const auto method = methods::Wiener::create({.psf = methods::Kernel{}, .blend = 0});
    REQUIRE(turn);
    if (!turn.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    REQUIRE(method);
    plan.rotation = turn.value();
    plan.photometric.restoration = *method;
}
void check_kernel_bypass_counts(const methods::WienerFacts& facts) {
    CHECK(facts.reason == methods::WienerReason::zero_blend);
    CHECK(facts.counts.eligible == 15);
    CHECK(facts.counts.evaluated == 0);
}
void check_kernel_bypass_shape(const methods::WienerFacts& facts) {
    CHECK(facts.psf.width == 3);
    CHECK(facts.psf.height == 5);
    CHECK(facts.psf.centroid_x == 1);
    CHECK(facts.psf.centroid_y == 2);
}
void check_kernel_bypass_observations(const pipeline::Rendering& rendered) {
    CHECK(rendered.output.shape.width == 3);
    CHECK(rendered.output.shape.height == 5);
    REQUIRE(rendered.stages.restoration.has_value());
    if (!rendered.stages.restoration.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    const auto& facts = rendered.stages.restoration.value();
    check_kernel_bypass_counts(facts);
    check_kernel_bypass_shape(facts);
}
void check_kernel_bypass_assets(const pipeline::Rendering& rendered,
                                const std::byte* const& address,
                                const std::optional<core::ContentIdentity>& identity,
                                std::span<const std::uint8_t> kernel) {
    REQUIRE(rendered.psf);
    if (!rendered.psf.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    CHECK(rendered.psf.value().encoded.bytes().data() == address);
    REQUIRE(identity);
    if (!identity.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    CHECK(rendered.psf.value().identity == *identity);
    CHECK(
        std::ranges::equal(rendered.psf.value().encoded.bytes(), std::as_bytes(std::span{kernel})));
}
void check_kernel_bypass_refusal(const pipeline::RenderResult& rendered) {
    REQUIRE(!rendered);
    CHECK(rendered.error().error.code == core::ErrorCode::input);
    CHECK(rendered.error().failed_stage == methods::Family::restoration);
    CHECK(!rendered.error().completed.restoration);
}
void check_kernel_bypass_result(const pipeline::RenderResult& rendered, bool positive,
                                const std::byte* const& address,
                                const std::optional<core::ContentIdentity>& identity,
                                std::span<const std::uint8_t> kernel) {
    if (positive) {
        REQUIRE(rendered);
        check_kernel_bypass_observations(*rendered);
        check_kernel_bypass_assets(*rendered, address, identity, kernel);
    } else {
        check_kernel_bypass_refusal(rendered);
    }
}
void check_kernel_bypass_case(bool positive) {
    core::Budget budget{std::size_t{16} * 1024 * 1024};
    std::vector<std::uint8_t> coefficients(std::size_t{3} * 5, 0);
    coefficients.back() = positive ? 255 : 0;
    const auto kernel = make_gray_png(
        {.width = 3, .height = 5, .depth = 8, .interlaced = false, .samples = coefficients});
    {
        auto input = restoration_input(budget, {.width = 5, .height = 3}, kernel);
        const std::byte* const address = input.encoded().psf.bytes().data();
        const auto identity = input.encoded().psf_identity;
        methods::Plan plan;
        configure_kernel_bypass(plan);
        const auto concurrency = exec::Concurrency::resolve(1, 1, 0, 0);
        REQUIRE(concurrency);
        const exec::Scheduler scheduler{*concurrency};
        const auto rendered = pipeline::render(std::move(input), plan, budget, scheduler);
        check_kernel_bypass_result(rendered, positive, address, identity, kernel);
    }
    CHECK(budget.used() == 0);
}
} // namespace
TEST_CASE("Actual restoration failure transports reported private context and completed work",
          "[app][pipeline]") {
    const auto raw = invocation();
    core::Budget budget{std::size_t{32} * 1024 * 1024};
    const auto failure = failed_restoration(raw, budget);
    for (unsigned change = 0; change < 5; ++change) {
        auto observed = failure;
        if (change == 1) {
            observed.warning_context.restoration_entered = false;
        } else if (change == 2) {
            observed.warning_context.restoration_after_transform =
                !observed.warning_context.restoration_after_transform;
        } else if (change == 3) {
            observed.failed_stage = methods::Family::contrast;
        } else if (change == 4) {
            observed.completed.denoising.reset();
        }
        ReturningProcessor processor{observed};
        UnusedVerifier verifier;
        const auto outcome = app::dispatch(raw, processor, verifier);
        CHECK(processor.calls() == 1);
        const auto& returned = std::get<app::ProcessFailure>(outcome.payload);
        check_failure_transport(returned, observed);
        CHECK(app::validate(failure_projection(observed)));
    }
}
TEST_CASE("Dispatch transports sparse restoration observations while the pipeline owns selected "
          "predecessors",
          "[app][pipeline]") {
    const auto raw = invocation();
    core::Budget budget{std::size_t{32} * 1024 * 1024};
    const auto failure = failed_restoration(raw, budget);
    for (unsigned state = 0; state < 3; ++state) {
        auto observed = failure;
        if (state == 1) {
            observed.completed.illumination.reset();
        } else if (state == 2) {
            observed.completed.denoising.reset();
        }
        ReturningProcessor processor{observed};
        UnusedVerifier verifier;
        const auto outcome = app::dispatch(raw, processor, verifier);
        const auto& returned = std::get<app::ProcessFailure>(outcome.payload);
        check_failure_transport(returned, observed);
        CHECK(app::validate(failure_projection(observed)));
        CHECK(processor.calls() == 1);
    }
}
TEST_CASE("Kernel bypass resolves the exact unrotated PSF in actual post-geometry F",
          "[pipeline][restoration]") {
    for (const bool positive : {false, true}) {
        check_kernel_bypass_case(positive);
    }
}
} // namespace docenhance::tests
