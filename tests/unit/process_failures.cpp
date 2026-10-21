// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/report/render.hpp"
#include "png_depth_fixture.hpp"
#include "stub_verifier.hpp"

#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
namespace docenhance::tests {
namespace {
template <typename T>
concept CarriesPublication = requires(T value) { value.publication; };
static_assert(!CarriesPublication<core::Error>);
static_assert(CarriesPublication<app::ProcessFailure>);
class ReturningFailure final : public app::Processor {
  public:
    app::ProcessFailure value{
        .error = {.code = core::ErrorCode::resource, .message = "Injected processing refusal"},
    };
    unsigned calls{};
    bool throw_after_entry = false;
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const app::ExecutionControl& /*control*/) override {
        ++calls;
        if (throw_after_entry) {
            throw std::runtime_error("Injected unreported processing exception");
        }
        return std::unexpected(std::move(value));
    }
};
app::Invocation binary_invocation() {
    app::Invocation value;
    value.command = app::Command::process;
    value.operands = {"input.png"};
    value.option(app::OptionId::out_dir) = "output";
    value.option(app::OptionId::output_mode) = "bw";
    value.option(app::OptionId::binarize) = "fixed";
    return value;
}
image::InterpretationReport interpretation() {
    return {
        .decoded = {.width = 2, .height = 2},
        .output = {.width = 2, .height = 2, .depth = fixture_png_depth(1)},
        .orientation = image::Orientation(),
        .rotation = image::QuarterTurn(),
        .resolution = std::nullopt,
        .assumed_transfer = true,
    };
}
const app::Failure& failed(const app::Outcome& outcome) {
    const auto* result = std::get_if<app::Failure>(&outcome.payload);
    REQUIRE(result);
    return *result;
}
} // namespace
TEST_CASE("Processing failures retain actual failed family without inventing completed reports") {
    ReturningFailure processor;
    UnusedVerifier verifier;
    processor.value.failed_stage = methods::Family::binarization;
    processor.value.warning_context.interpretation = interpretation();
    const auto outcome = app::dispatch(binary_invocation(), processor, verifier);
    CHECK(processor.calls == 1);
    CHECK(failed(outcome).error.code == core::ErrorCode::resource);
    CHECK(failed(outcome).publication == core::Publication::not_started);
    CHECK(failed(outcome).failed_stage == methods::Family::binarization);
    CHECK(!failed(outcome).completed.binarization);
}
TEST_CASE("An escaping processing exception retains preallocated unknown publication") {
    UnusedVerifier verifier;
    ReturningFailure processor;
    processor.throw_after_entry = true;
    const auto outcome = app::dispatch(binary_invocation(), processor, verifier);
    CHECK(processor.calls == 1);
    CHECK(failed(outcome).error.code == core::ErrorCode::publication_unknown);
    CHECK(failed(outcome).publication == core::Publication::unknown);
    CHECK(outcome.exit_code() == core::ExitCode::publication_unknown);
}
TEST_CASE("Reported processing cause and actual effect survive one dispatch and rendering") {
    for (const bool committed : {false, true}) {
        ReturningFailure processor;
        UnusedVerifier verifier;
        const auto code = committed ? core::ErrorCode::resource : core::ErrorCode::input;
        const auto publication =
            committed ? core::Publication::completed : core::Publication::unknown;
        processor.value.error.code = code;
        processor.value.publication = publication;
        const auto outcome = app::dispatch(binary_invocation(), processor, verifier);
        CHECK(processor.calls == 1);
        CHECK(failed(outcome).error.code == code);
        CHECK(failed(outcome).publication == publication);
        const auto text = report::render(outcome, report::Format::text);
        REQUIRE(text);
        const auto json = report::render(outcome, report::Format::json);
        REQUIRE(json);
        const auto fields = record::Json::parse(json->out);
        CHECK(fields.at("publication") == (committed ? "completed" : "unknown"));
        CHECK(fields.at("error").at("code") == (committed ? "E_RESOURCE" : "E_INPUT"));
        CHECK(fields.at("exit_code") ==
              static_cast<int>(core::Error{.code = code, .message = {}}.exit_code()));
        CHECK(failed(outcome).publication == publication);
    }
}
TEST_CASE("A later output verification failure retains a confirmed commit") {
    ReturningFailure processor;
    UnusedVerifier verifier;
    processor.value.error.code = core::ErrorCode::output_verify;
    processor.value.publication = core::Publication::completed;
    processor.value.warning_context.interpretation = interpretation();
    methods::FixedFacts classification;
    classification.black_samples = 2;
    classification.white_samples = 2;
    processor.value.completed.binarization = classification;
    const auto outcome = app::dispatch(binary_invocation(), processor, verifier);
    CHECK(failed(outcome).error.code == core::ErrorCode::output_verify);
    CHECK(failed(outcome).publication == core::Publication::completed);
    REQUIRE(failed(outcome).completed.binarization);
    CHECK(outcome.exit_code() == core::ExitCode::output);
}
TEST_CASE("Partial TV stop remains runtime context with no fabricated completed denoising") {
    ReturningFailure processor;
    UnusedVerifier verifier;
    auto invocation = binary_invocation();
    invocation.option(app::OptionId::denoise) = "tvl1";
    processor.value.failed_stage = methods::Family::denoising;
    processor.value.warning_context.interpretation = interpretation();
    processor.value.warning_context.observed_tvl1_stop = methods::Tvl1Stop::iteration_limit;
    const auto outcome = app::dispatch(invocation, processor, verifier);
    CHECK(failed(outcome).error.code == core::ErrorCode::resource);
    CHECK(!failed(outcome).completed.denoising);
    CHECK(failed(outcome).warning_context.observed_tvl1_stop == methods::Tvl1Stop::iteration_limit);
}
TEST_CASE("Readonly dispatch transfers one first-party observation without processing") {
    class ObservingVerifier final : public app::Verifier {
      public:
        unsigned calls{};
        core::Result<app::Verified> verify(const app::VerifyRequest& request,
                                           const core::Cancellation& /*cancellation*/) override {
            ++calls;
            return app::Verified{
                .directory = request.directory(),
                .run = std::string(32, 'a'),
                .recorded = "2026-10-19T00:00:00Z",
                .confirmed =
                    {
                        {
                            .name = "result.png",
                            .identity =
                                {
                                    .sha256 = std::string(64, 'b'),
                                    .bytes = 1,
                                },
                        },
                        {
                            .name = "run.json",
                            .identity =
                                {
                                    .sha256 = std::string(64, 'c'),
                                    .bytes = 1,
                                },
                        },
                    },
            };
        }
    } verifier;
    ReturningFailure processor;
    app::Invocation invocation;
    invocation.command = app::Command::verify;
    invocation.operands = {"bundle"};
    const auto accepted = app::dispatch(invocation, processor, verifier);
    REQUIRE(accepted.exit_code() == core::ExitCode::success);
    CHECK(std::get<app::Verified>(accepted.payload).directory == "bundle");
    CHECK(verifier.calls == 1);
    CHECK(processor.calls == 0);
}
} // namespace docenhance::tests
