// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/sharpen_facts.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/record/codec.hpp"
#include "execution_fixture.hpp"
#include "stub_verifier.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <variant>
namespace docenhance::tests {
namespace {
enum class Family { illumination, nlm, tvl1, restoration, gamma, sharpening };
constexpr image::Extent model_extent{.width = 32, .height = 32};
constexpr std::uint64_t model_pixels = std::uint64_t{32} * 32;
app::Invocation model_request(Family family) {
    app::Invocation request;
    request.command = app::Command::process;
    request.operands = {"input.png"};
    request.option(app::OptionId::out_dir) = "output";
    switch (family) {
    case Family::illumination:
        request.option(app::OptionId::illumination) = "surface";
        request.option(app::OptionId::background_target) = "1";
        request.option(app::OptionId::background_cell) = "8";
        break;
    case Family::nlm:
        request.option(app::OptionId::denoise) = "nlm";
        break;
    case Family::tvl1:
        request.option(app::OptionId::denoise) = "tvl1";
        break;
    case Family::restoration:
        request.option(app::OptionId::deblur) = "wiener";
        request.option(app::OptionId::psf) = "gaussian";
        break;
    case Family::gamma:
        request.option(app::OptionId::contrast) = "gamma";
        request.option(app::OptionId::gamma) = "2";
        break;
    case Family::sharpening:
        request.option(app::OptionId::sharpen) = "unsharp";
        break;
    }
    return request;
}
methods::IlluminationFacts& selected_illumination(methods::StageReports& reports) {
    REQUIRE(reports.illumination.has_value());
    if (!reports.illumination.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    return reports.illumination.value();
}
methods::DenoisingFacts& selected_denoising(methods::StageReports& reports) {
    REQUIRE(reports.denoising.has_value());
    if (!reports.denoising.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    return reports.denoising.value();
}
methods::WienerFacts& selected_restoration(methods::StageReports& reports) {
    REQUIRE(reports.restoration.has_value());
    if (!reports.restoration.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    return reports.restoration.value();
}
methods::ContrastFacts& selected_contrast(methods::StageReports& reports) {
    REQUIRE(reports.contrast.has_value());
    if (!reports.contrast.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    return reports.contrast.value();
}
methods::UnsharpFacts& selected_sharpening(methods::StageReports& reports) {
    REQUIRE(reports.sharpening.has_value());
    if (!reports.sharpening.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    return reports.sharpening.value();
}
template <class Action>
void selected_report(Family family, methods::StageReports& reports, Action action) {
    switch (family) {
    case Family::illumination:
        std::visit(action, selected_illumination(reports));
        break;
    case Family::nlm:
    case Family::tvl1:
        std::visit(action, selected_denoising(reports));
        break;
    case Family::restoration:
        action(selected_restoration(reports));
        break;
    case Family::gamma:
        std::visit(action, selected_contrast(reports));
        break;
    case Family::sharpening:
        action(selected_sharpening(reports));
        break;
    }
}
class ReturningProcessor final : public app::Processor {
  public:
    explicit ReturningProcessor(app::Published value) : value_(std::move(value)) {}
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const app::ExecutionControl& /*control*/) override {
        ++calls_;
        return std::move(value_);
    }
    [[nodiscard]] unsigned calls() const noexcept {
        return calls_;
    }

  private:
    unsigned calls_ = 0;
    app::Published value_;
};
} // namespace
TEST_CASE("Selected eager families complete full observations before encoded output",
          "[app][pipeline]") {
    for (const auto family : {
             Family::illumination,
             Family::nlm,
             Family::tvl1,
             Family::restoration,
             Family::gamma,
             Family::sharpening,
         }) {
        const auto invocation = model_request(family);
        auto observed = observed_publication(invocation, model_extent);
        selected_report(family, observed.record.execution, [](const auto& facts) {
            CHECK(facts.counts.eligible == model_pixels);
            CHECK(facts.counts.evaluated == model_pixels);
            CHECK(facts.counts.protected_samples == 0);
        });
        const auto completed = static_cast<const methods::StageReports&>(observed.record.execution);
        const auto encoded = record::Codec::encode(observed.record);
        REQUIRE(encoded);
        const auto decoded = record::Codec::decode<record::RunRecord>(*encoded);
        REQUIRE(decoded);
        CHECK(decoded->execution == observed.record.execution);
        CHECK(static_cast<const methods::StageReports&>(observed.record.execution) == completed);
        ReturningProcessor processor{observed};
        UnusedVerifier verifier;
        const auto outcome = app::dispatch(invocation, processor, verifier);
        CHECK(processor.calls() == 1);
        CHECK(std::holds_alternative<app::Published>(outcome.payload));
        selected_report(family, observed.record.execution,
                        [](auto& facts) { facts.counts.evaluated = 0; });
        const auto refused = record::Codec::encode(observed.record);
        REQUIRE(!refused);
        CHECK(refused.error().code == core::ErrorCode::invariant);
    }
}
TEST_CASE("Completed illumination cannot claim an unobserved gain application", "[app][pipeline]") {
    auto observed = observed_publication(model_request(Family::illumination), model_extent);
    REQUIRE(observed.record.execution.illumination.has_value());
    if (!observed.record.execution.illumination.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    std::visit(
        [](auto& facts) {
            facts.counts.evaluated = 0;
            facts.min_gain = facts.max_gain = 2;
        },
        observed.record.execution.illumination.value());
    const auto refused = record::Codec::encode(observed.record);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::invariant);
}
} // namespace docenhance::tests
