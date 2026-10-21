// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/morphology_facts.hpp"
#include "docenhance/methods/nlm_facts.hpp"
#include "docenhance/methods/surface_facts.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/inventory.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/method_requests.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/request.hpp"
#include "docenhance/report/render.hpp"
#include "execution_exception.hpp"
#include "execution_fixture.hpp"
#include "morphology_fixture.hpp"
#include "processor.hpp"
#include "stub_verifier.hpp"
#include "surface_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
namespace docenhance::tests {
namespace {
class ReturningVerifier final : public app::Verifier {
  public:
    explicit ReturningVerifier(core::Result<app::Verified> value) : value_(std::move(value)) {}
    void exception(unsigned value) noexcept {
        exception_ = value;
    }
    core::Result<app::Verified> verify(const app::VerifyRequest& /*request*/,
                                       const core::Cancellation& /*cancellation*/) override {
        ++calls_;
        if (exception_ == 1) {
            throw std::bad_alloc{};
        }
        if (exception_ == 2) {
            throw std::runtime_error("Unreported readonly failure");
        }
        if (exception_ == 3) {
            throw_nonstandard_execution_failure();
        }
        return value_;
    }
    [[nodiscard]] unsigned calls() const noexcept {
        return calls_;
    }

  private:
    unsigned calls_ = 0;
    core::Result<app::Verified> value_;
    unsigned exception_{};
};
app::Invocation invocation(bool binary) {
    app::Invocation value;
    value.command = app::Command::process;
    value.operands = {"input.png"};
    value.option(app::OptionId::out_dir) = "output";
    value.option(app::OptionId::output_mode) = binary ? "bw" : "preserve";
    if (binary) {
        value.option(app::OptionId::binarize) = "fixed";
    }
    return value;
}
const app::Failure& failed(const app::Outcome& value) {
    const auto* failure = std::get_if<app::Failure>(&value.payload);
    REQUIRE(failure);
    return *failure;
}
} // namespace
TEST_CASE("Published record identities are admitted at their owning record receiver") {
    const auto valid = observed_publication(invocation(true));
    REQUIRE(record::Codec::encode(valid.record));
    auto invalid = valid.record;
    invalid.context.identity.clear();
    CHECK(!record::Codec::encode(invalid));
    // Actual serialized bytes, not a second application check, own the record-size identity.
    const auto serialized = record::serialize(valid.record);
    REQUIRE(serialized);
    CHECK(valid.record_identity.bytes == serialized->size());
    CHECK(valid.record_identity.bytes > 0);
    CHECK(!record::read_record(std::span<const std::byte>{}));
    CHECK(valid.record_identity.bytes <= record::record_max_bytes);
    auto oversized = valid.record;
    oversized.build.compiler.assign(record::record_max_bytes, 'x');
    REQUIRE(record::validate(oversized));
    const auto refused = record::serialize(oversized);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::invariant);
}
TEST_CASE("Actual continuous records retain selected geometry and completed method facts") {
    auto request = invocation(false);
    request.option(app::OptionId::illumination) = "morph";
    request.option(app::OptionId::background_strength) = "0";
    const auto valid = observed_publication(request);
    REQUIRE(record::Codec::encode(valid.record));
    for (unsigned change = 0; change < 5; ++change) {
        auto value = valid.record;
        auto& report = value.execution.interpretation;
        REQUIRE(value.execution.illumination.has_value());
        if (!value.execution.illumination.has_value()) {
            throw std::logic_error("Required test observation is absent");
        }
        auto& light = std::get<methods::MorphologyFacts>(value.execution.illumination.value());
        if (change == 0) {
            report.output.width = 4;
        }
        if (change == 1) {
            report.output.model = image::SampleModel::rgb;
        }
        if (change == 2) {
            light.counts.eligible = 7;
        }
        if (change == 3) {
            light.max_gain = std::numeric_limits<double>::quiet_NaN();
        }
        if (change == 4) {
            report.depth_reduced = true;
        }
        CHECK(!record::Codec::encode(value));
    }
    auto foreign_family = valid.record;
    foreign_family.request.illumination = record::SurfaceRequest{};
    CHECK(!record::Codec::encode(foreign_family));
    // An independently coherent alpha choice is valid; the real producer records its own Plan.
    request.option(app::OptionId::alpha) = "black";
    const auto black = observed_publication(request);
    REQUIRE(record::Codec::encode(black.record));
    CHECK(std::get<record::PreserveOutputRequest>(black.record.request.output).alpha ==
          image::AlphaPolicy::black);
    CHECK(std::get<record::PreserveOutputRequest>(valid.record.request.output).alpha ==
          image::AlphaPolicy::white);
}
TEST_CASE("Actual NLM records reject inconsistent extent and active table claims") {
    auto request = invocation(false);
    request.option(app::OptionId::denoise) = "nlm";
    const auto valid = observed_publication(request, {.width = 21, .height = 21});
    REQUIRE(record::Codec::encode(valid.record));
    REQUIRE(valid.record.execution.denoising.has_value());
    if (!valid.record.execution.denoising.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    const auto& facts = std::get<methods::NlmFacts>(valid.record.execution.denoising.value());
    const auto method = methods::Nlm::create();
    REQUIRE(method);
    REQUIRE(method->parameters().blend > 0);
    REQUIRE(facts.counts.evaluated == 441);
    REQUIRE(facts.weight_table_entries > 0);
    REQUIRE(facts.weight_table_entries == methods::nlm_weight_entries(*method));
    for (unsigned change = 0; change < 2; ++change) {
        auto bad = valid.record;
        if (!bad.execution.denoising.has_value()) {
            throw std::logic_error("Required copied observation is absent");
        }
        auto& altered = std::get<methods::NlmFacts>(bad.execution.denoising.value());
        if (change == 0) {
            altered.counts.eligible += 1;
        } else {
            altered.weight_table_entries = 0;
        }
        CHECK(!record::Codec::encode(bad));
    }
}
TEST_CASE("Readonly execution contains exceptions and transports reported errors without effects") {
    RejectingProcessor processor;
    app::Invocation request;
    request.command = app::Command::verify;
    request.operands = {"bundle"};
    for (const unsigned kind : {1U, 2U, 3U}) {
        ReturningVerifier verifier{core::failure(core::ErrorCode::input, "Refused")};
        verifier.exception(kind);
        const auto outcome = app::dispatch(request, processor, verifier);
        CHECK(verifier.calls() == 1);
        CHECK(failed(outcome).error.code ==
              (kind == 1 ? core::ErrorCode::resource : core::ErrorCode::invariant));
        CHECK(failed(outcome).publication == core::Publication::not_started);
        const auto text = report::render(outcome, report::Format::text);
        REQUIRE(text);
        CHECK(text->out.empty());
        CHECK(!text->err.empty());
        const auto json = report::render(outcome, report::Format::json);
        REQUIRE(json);
        CHECK(json->err.empty());
        const auto fields = record::Json::parse(json->out);
        CHECK(fields.at("publication") == "not_started");
        CHECK(fields.at("error").at("code") == (kind == 1 ? "E_RESOURCE" : "E_INVARIANT"));
        CHECK(processor.calls == 0);
    }
    for (const auto code : {
             core::ErrorCode::publication_unknown,
             core::ErrorCode::output_verify,
             core::ErrorCode::method_inapplicable,
             core::ErrorCode::numerical,
         }) {
        ReturningVerifier verifier{core::failure(code, "Reported readonly error")};
        const auto outcome = app::dispatch(request, processor, verifier);
        CHECK(verifier.calls() == 1);
        CHECK(failed(outcome).error.code == code);
        CHECK(failed(outcome).publication == core::Publication::not_started);
    }
    CHECK(processor.calls == 0);
}
TEST_CASE("Invalid command values have no scope bits and cannot reach execution") {
    RejectingProcessor processor;
    UnusedVerifier verifier;
    auto request = invocation(false);
    request.command = static_cast<app::Command>(std::numeric_limits<unsigned>::digits);
    CHECK(!app::CommandSet::all().contains(request.command));
    const auto outcome = app::dispatch(request, processor, verifier);
    CHECK(failed(outcome).error.code == core::ErrorCode::argument);
    CHECK(outcome.command == app::Command::root);
    CHECK(processor.calls == 0);
}
TEST_CASE("Morphology completed facts retain original parameter and extent admission") {
    const auto valid = completed_morphology_facts();
    REQUIRE(methods::validate(valid, morphology_fact_parameters, morphology_fact_extent));
    for (unsigned change = 0; change < 5; ++change) {
        auto bad = valid;
        REQUIRE(bad.analysis.has_value());
        if (!bad.analysis.has_value()) {
            throw std::logic_error("Required test observation is absent");
        }
        if (change == 0) {
            bad.counts.eligible = 257;
        }
        if (change == 1) {
            bad.analysis.value().stride = 2;
        }
        if (change == 2) {
            bad.sigma = 129;
        }
        if (change == 3) {
            bad.analysis.value().background_q10 = -0.1;
        }
        if (change == 4) {
            bad.analysis.value().count = 1;
        }
        CHECK(!methods::validate(bad, morphology_fact_parameters, morphology_fact_extent));
    }
}
TEST_CASE("Surface completed facts retain parameter agreement and finite solver values") {
    const auto valid = completed_surface_facts();
    REQUIRE(methods::validate(valid, surface_fact_parameters, surface_fact_extent));
    auto zero = surface_fact_parameters;
    zero.strength = 0;
    CHECK(!methods::validate(valid, zero, surface_fact_extent));
    auto nonfinite = valid;
    REQUIRE(nonfinite.solver.has_value());
    if (!nonfinite.solver.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    nonfinite.solver.value().residual = std::numeric_limits<double>::quiet_NaN();
    CHECK(!methods::validate(nonfinite, surface_fact_parameters, surface_fact_extent));
}
} // namespace docenhance::tests
