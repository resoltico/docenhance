// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "processor.hpp"
#include "stub_verifier.hpp"

#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
namespace docenhance::tests {
namespace {
class ReturningProcessor final : public app::Processor {
  public:
    explicit ReturningProcessor(app::ProcessResult value) : value_(std::move(value)) {}
    [[nodiscard]] unsigned calls() const noexcept {
        return calls_;
    }
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const core::Cancellation& /*cancellation*/) override {
        ++calls_;
        return value_;
    }

  private:
    app::ProcessResult value_;
    unsigned calls_ = 0;
};
class ReturningVerifier final : public app::Verifier {
  public:
    explicit ReturningVerifier(core::Result<app::Verified> value) : value_(std::move(value)) {}
    void exception(unsigned value) noexcept {
        exception_ = value;
    }
    core::Result<app::Verified> verify(const app::VerifyRequest& /*request*/,
                                       const core::Cancellation& /*cancellation*/) override {
        if (exception_ == 1) {
            throw std::bad_alloc{};
        }
        if (exception_ == 2) {
            throw std::runtime_error("Unreported readonly failure");
        }
        return value_;
    }

  private:
    core::Result<app::Verified> value_;
    unsigned exception_ = 0;
};
contract::Invocation invocation(bool binary) {
    contract::Invocation value;
    value.command = contract::Command::process;
    value.subject = "input.png";
    value.output_directory = "output";
    value.output_mode = binary ? "bw" : "preserve";
    return value;
}
app::PublishedContinuous continuous() {
    app::PublishedContinuous value;
    value.output = "output/result.png";
    value.run = std::string(32, 'a');
    value.record = {.sha256 = std::string(64, 'b'), .bytes = 512};
    value.conversion.source = {.width = 2, .height = 2};
    value.conversion.output = value.conversion.source;
    value.conversion.assumed_transfer = true;
    value.conversion.verified = true;
    value.illumination.complete = true;
    value.illumination.eligible_samples = 4;
    value.denoising.complete = true;
    value.denoising.eligible_samples = 4;
    return value;
}
const core::Error& error(const app::Outcome& value) {
    return std::get<app::Failure>(value.payload).error;
}
} // namespace
TEST_CASE("Contradictory processing failures retain unknown state without repeating effects") {
    UnusedVerifier verifier;
    for (const auto code : {
             core::ErrorCode::cancelled,
             core::ErrorCode::resource,
             core::ErrorCode::publication_unknown,
             core::ErrorCode::output,
         }) {
        for (const auto state : {
                 core::Publication::not_started,
                 core::Publication::not_published,
                 core::Publication::completed,
                 core::Publication::unknown,
             }) {
            const core::Error reported{
                .code = code,
                .message = "Reported failure",
                .publication = state,
            };
            ReturningProcessor processor{app::process_failure(reported)};
            const auto outcome = app::dispatch(invocation(true), processor, verifier);
            CHECK(processor.calls() == 1);
            if (reported.valid_publication()) {
                CHECK(error(outcome).code == code);
                CHECK(error(outcome).publication == state);
            } else {
                CHECK(error(outcome).code == core::ErrorCode::publication_unknown);
                CHECK(error(outcome).publication == core::Publication::unknown);
            }
        }
    }
    ReturningProcessor committed{app::process_failure({
        .code = core::ErrorCode::output_verify,
        .message = "Integrity failure",
        .publication = core::Publication::completed,
    })};
    CHECK(error(app::dispatch(invocation(true), committed, verifier)).publication ==
          core::Publication::completed);
}
TEST_CASE(
    "Processing success refuses malformed identities and outcomes outside the admitted target") {
    UnusedVerifier verifier;
    const app::PublishedBinary valid{
        .output = "output/result.png",
        .run = std::string(32, 'a'),
        .record = {.sha256 = std::string(64, 'b'), .bytes = 512},
    };
    ReturningProcessor control{valid};
    CHECK(app::dispatch(invocation(true), control, verifier).exit_code() ==
          core::ExitCode::success);
    for (unsigned change = 0; change < 4; ++change) {
        auto value = valid;
        if (change == 0) {
            value.output = "elsewhere/result.png";
        }
        if (change == 1) {
            value.run.clear();
        }
        if (change == 2) {
            value.record.sha256.front() = 'B';
        }
        if (change == 3) {
            value.record.bytes = 0;
        }
        ReturningProcessor processor{value};
        CHECK(error(app::dispatch(invocation(true), processor, verifier)).publication ==
              core::Publication::unknown);
    }
}
TEST_CASE("Complete continuous returns must agree on geometry, operation and stage counts") {
    UnusedVerifier verifier;
    ReturningProcessor control{continuous()};
    CHECK(app::dispatch(invocation(false), control, verifier).exit_code() ==
          core::ExitCode::success);
    for (unsigned change = 0; change < 5; ++change) {
        auto value = continuous();
        if (change == 0) {
            value.conversion.output.width = 3;
        }
        if (change == 1) {
            value.conversion.output.model = image::SampleModel::rgb;
        }
        if (change == 2) {
            value.illumination.eligible_samples = 5;
        }
        if (change == 3) {
            value.illumination.max_gain = std::numeric_limits<double>::quiet_NaN();
        }
        if (change == 4) {
            value.conversion.orientation = 0;
        }
        ReturningProcessor processor{value};
        CHECK(error(app::dispatch(invocation(false), processor, verifier)).publication ==
              core::Publication::unknown);
    }
    auto native_request = invocation(false);
    native_request.denoise = "nlm";
    auto impossible_native = continuous();
    auto& native = impossible_native.denoising;
    native.requested = methods::NlmParameters{};
    native.native_h = methods::nlm_native_strength(*native.requested);
    native.status = methods::DenoiseStatus::applied;
    native.evaluated_samples = 4;
    native.corrected_samples = 1;
    native.changed_samples = 1;
    native.native_calls = 1;
    native.native_reserved_peak = 1;
    native.preparation_charge_peak = 1;
    ReturningProcessor wrong_extent{impossible_native};
    CHECK(error(app::dispatch(native_request, wrong_extent, verifier)).publication ==
          core::Publication::unknown);
    auto request = invocation(false);
    request.illumination = "surface";
    methods::IlluminationReport wrong;
    wrong.requested = methods::SurfaceParameters{};
    wrong.requested->strength = 0.5;
    wrong.status = methods::SurfaceStatus::failed;
    ReturningProcessor mismatch{app::process_failure(
        {.code = core::ErrorCode::resource, .message = "Partial failure"}, wrong)};
    CHECK(error(app::dispatch(request, mismatch, verifier)).publication ==
          core::Publication::unknown);
    wrong.requested = methods::SurfaceParameters{};
    wrong.solver = methods::SolverReport{.residual = std::numeric_limits<double>::quiet_NaN()};
    ReturningProcessor nonfinite{app::process_failure(
        {
            .code = core::ErrorCode::numerical,
            .message = "Malformed partial diagnostic",
        },
        wrong)};
    CHECK(error(app::dispatch(request, nonfinite, verifier)).publication ==
          core::Publication::unknown);
}
TEST_CASE("Readonly returns and exceptions cannot claim publication or fabricated confirmation") {
    RejectingProcessor processor;
    contract::Invocation request;
    request.command = contract::Command::verify;
    request.subject = "bundle";
    const app::Verified valid{
        .directory = "bundle",
        .run = std::string(32, 'a'),
        .recorded = "2026-10-02T00:00:00Z",
        .confirmed =
            {
                {.name = "result.png", .identity = {.sha256 = std::string(64, 'b'), .bytes = 512}},
            },
    };
    ReturningVerifier control{valid};
    CHECK(app::dispatch(request, processor, control).exit_code() == core::ExitCode::success);
    for (unsigned change = 0; change < 5; ++change) {
        auto value = valid;
        if (change == 0) {
            value.directory = "different";
        }
        if (change == 1) {
            value.recorded = "2026-02-29T00:00:00Z";
        }
        if (change == 2) {
            value.confirmed.clear();
        }
        if (change == 3) {
            value.confirmed.push_back(value.confirmed.front());
        }
        if (change == 4) {
            value.confirmed.front().name = "../outside";
        }
        ReturningVerifier verifier{value};
        const auto outcome = app::dispatch(request, processor, verifier);
        CHECK(error(outcome).code == core::ErrorCode::invariant);
        CHECK(error(outcome).publication == core::Publication::not_started);
    }
    ReturningVerifier invalid{core::failure(core::ErrorCode::input, "Refused")};
    for (const unsigned kind : {1U, 2U}) {
        invalid.exception(kind);
        const auto outcome = app::dispatch(request, processor, invalid);
        CHECK(error(outcome).code ==
              (kind == 1 ? core::ErrorCode::resource : core::ErrorCode::invariant));
        CHECK(error(outcome).publication == core::Publication::not_started);
    }
    ReturningVerifier dishonest{std::unexpected(core::Error{
        .code = core::ErrorCode::cancelled,
        .message = "Invalid readonly state",
        .publication = core::Publication::completed,
    })};
    CHECK(error(app::dispatch(request, processor, dishonest)).code == core::ErrorCode::invariant);
    CHECK(processor.calls == 0);
}
TEST_CASE("Invalid command values have no scope bits and cannot reach execution") {
    RejectingProcessor processor;
    UnusedVerifier verifier;
    auto request = invocation(false);
    request.command = static_cast<contract::Command>(std::numeric_limits<unsigned>::digits);
    CHECK(!contract::CommandSet::all().contains(request.command));
    const auto outcome = app::dispatch(request, processor, verifier);
    CHECK(error(outcome).code == core::ErrorCode::argument);
    CHECK(outcome.command == contract::Command::root);
    CHECK(processor.calls == 0);
}
} // namespace docenhance::tests
