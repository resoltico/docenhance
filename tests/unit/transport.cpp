// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "delivery_fixture.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/cli/run.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "stub_verifier.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <ios>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <streambuf>

namespace docenhance::tests {
namespace {
UnusedVerifier verifier;
} // namespace
namespace {
class CountingProcessor final : public app::Processor {
  public:
    unsigned calls = 0;
    app::ProcessResult process(const app::ProcessRequest& request,
                               const app::ExecutionControl& /*control*/) override {
        ++calls;
        return delivery_publication(request);
    }
};
class CapturingProcessor final : public app::Processor {
  public:
    std::optional<app::ProcessRequest> received;
    app::ProcessResult process(const app::ProcessRequest& request,
                               const app::ExecutionControl& /*control*/) override {
        received = request;
        return app::process_failure(
            {
                .code = core::ErrorCode::not_implemented,
                .message = "Captured without processing effects",
            },
            core::Publication::not_started);
    }
};
class RefusingBuffer final : public std::streambuf {
  protected:
    std::streamsize xsputn(const char* /*__s*/, std::streamsize /*__n*/) override {
        return 0;
    }
    int_type overflow(int_type /*__c*/) override {
        return traits_type::eof();
    }
};
} // namespace
TEST_CASE("Invalid invocations never reach processing", "[cli]") {
    CountingProcessor processor;
    std::ostringstream out;
    std::ostringstream err;
    const auto args = std::to_array<const char*>({"docenhance", "process", "input.png", "--json"});
    CHECK(cli::run(args, {.processor = processor, .verifier = verifier}, out, err) ==
          static_cast<int>(core::ExitCode::invocation));
    CHECK(processor.calls == 0);
    CHECK(err.str().empty());
    CHECK(cli::run({}, {.processor = processor, .verifier = verifier}, out, err) ==
          static_cast<int>(core::ExitCode::invocation));
    const auto invalid = std::to_array<const char*>({"docenhance", nullptr});
    CHECK(cli::run(invalid, {.processor = processor, .verifier = verifier}, out, err) ==
          static_cast<int>(core::ExitCode::invocation));
}
TEST_CASE("A required string value can consume the option marker before a JSON flag", "[cli]") {
    const auto bare = std::to_array<const char*>(
        {"docenhance", "process", "input.png", "--out-dir", "--", "--json"});
    const auto check_case = [](std::span<const char* const> args) {
        CapturingProcessor processor;
        std::ostringstream out;
        std::ostringstream err;
        CHECK(cli::run(args, {.processor = processor, .verifier = verifier}, out, err) ==
              static_cast<int>(core::ExitCode::processing));
        if (!processor.received.has_value()) {
            FAIL("The admitted request did not reach processing");
            return;
        }
        CHECK(processor.received->output_directory() == "--");
        CHECK(out.str().starts_with('{'));
        CHECK(err.str().empty());
    };
    check_case(bare);
}
TEST_CASE("A JSON-looking required value does not select JSON presentation", "[cli]") {
    CountingProcessor processor;
    std::ostringstream out;
    std::ostringstream err;
    const auto args =
        std::to_array<const char*>({"docenhance", "process", "--profile-policy", "--json"});
    CHECK(cli::run(args, {.processor = processor, .verifier = verifier}, out, err) ==
          static_cast<int>(core::ExitCode::invocation));
    CHECK(processor.calls == 0);
    CHECK(out.str().empty());
    CHECK(err.str().starts_with("E_"));
}
TEST_CASE("An empty joined assignment retains the following JSON flag", "[cli]") {
    CapturingProcessor processor;
    std::ostringstream out;
    std::ostringstream err;
    const auto args =
        std::to_array<const char*>({"docenhance", "process", "input.png", "--out-dir=", "--json"});
    CHECK(cli::run(args, {.processor = processor, .verifier = verifier}, out, err) ==
          static_cast<int>(core::ExitCode::invocation));
    CHECK(!processor.received.has_value());
    CHECK(out.str().starts_with('{'));
    CHECK(out.str().contains("E_ARGUMENT"));
    CHECK(out.str().contains("not_started"));
    CHECK(err.str().empty());
}
TEST_CASE("Known syntax failures retain raw JSON fallback", "[cli]") {
    for (const auto arguments : {
             std::to_array<const char*>({"docenhance", "process", "--wat", "--json"}),
             std::to_array<const char*>({"\xff", "process", "--out-dir", "--json"}),
         }) {
        CountingProcessor processor;
        std::ostringstream out;
        std::ostringstream err;
        CHECK(cli::run(arguments, {.processor = processor, .verifier = verifier}, out, err) == 2);
        CHECK(processor.calls == 0);
        CHECK(out.str().starts_with('{'));
        CHECK(out.str().contains("E_ARGUMENT"));
        CHECK(out.str().contains("not_started"));
        CHECK(err.str().empty());
    }
}
TEST_CASE("Consumed and unconsumed markers keep admission and syntax presentation distinct",
          "[cli]") {
    const auto admission =
        std::to_array<const char*>({"docenhance", "process", "--profile-policy", "--", "--json"});
    const auto syntax = std::to_array<const char*>(
        {"docenhance", "process", "--profile-policy", "--", "--wat", "--json"});
    const auto operand = std::to_array<const char*>(
        {"docenhance", "process", "--profile-policy", "embedded", "--", "--json"});
    const auto check_case = [](std::span<const char* const> args, bool json) {
        CountingProcessor processor;
        std::ostringstream out;
        std::ostringstream err;
        CHECK(cli::run(args, {.processor = processor, .verifier = verifier}, out, err) ==
              static_cast<int>(core::ExitCode::invocation));
        CHECK(processor.calls == 0);
        if (json) {
            CHECK(out.str().starts_with('{'));
            CHECK(err.str().empty());
        } else {
            CHECK(out.str().empty());
            CHECK(err.str().starts_with("E_"));
        }
    };
    check_case(admission, true);
    check_case(syntax, false);
    check_case(operand, false);
}
TEST_CASE("Output stream failure cannot repeat processing or render another outcome", "[cli]") {
    CountingProcessor processor;
    RefusingBuffer buffer;
    std::ostream out{&buffer};
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto failure_mask = static_cast<std::ios::iostate>(
        static_cast<unsigned>(std::ios::badbit) | static_cast<unsigned>(std::ios::failbit));
    out.exceptions(failure_mask);
    std::ostringstream err;
    const auto args = std::to_array<const char*>({
        "docenhance",
        "process",
        "input.png",
        "--out-dir",
        "result",
        "--output-mode",
        "bw",
        "--binarize",
        "fixed",
        "--json",
    });
    CHECK(cli::run(args, {.processor = processor, .verifier = verifier}, out, err) ==
          static_cast<int>(core::ExitCode::output));
    CHECK(processor.calls == 1);
    CHECK(err.str().empty());
}
} // namespace docenhance::tests

namespace docenhance::tests {
TEST_CASE("CLI typed bindings transfer nondefault values to their admitted owners", "[cli]") {
    CapturingProcessor processor;
    std::ostringstream out;
    std::ostringstream err;
    const auto args = std::to_array<const char*>({
        "docenhance",
        "process",
        "input.png",
        "--out-dir",
        "result",
        "--json",
        "--output-mode",
        "gray",
        "--bit-depth",
        "16",
        "--alpha",
        "black",
        "--profile-policy",
        "srgb",
        "--illumination",
        "auto",
        "--background-strength",
        "0.25",
        "--background-max-gain",
        "3",
        "--background-target",
        "0.7",
        "--background-cell",
        "32",
        "--background-quantile",
        "0.8",
        "--background-smooth",
        "4",
        "--protect-mask",
        "mask.png",
        "--denoise",
        "nlm",
        "--nlm-h",
        "4",
        "--nlm-patch",
        "5",
        "--nlm-search",
        "9",
        "--denoise-blend",
        "0.75",
    });
    CHECK(cli::run(args, {.processor = processor, .verifier = verifier}, out, err) ==
          static_cast<int>(core::ExitCode::processing));
    if (!processor.received.has_value()) {
        FAIL("Valid nondefault options did not reach admission");
        return;
    }
    const auto& request = *processor.received;
    CHECK(request.input() == "input.png");
    CHECK(request.output_directory() == "result");
    CHECK(request.mask_path() == "mask.png");
    const auto& plan = request.plan();
    CHECK(plan.protection);
    const auto& tone = std::get<methods::ContinuousOutput>(plan.output);
    CHECK(tone.mode == image::ToneMode::gray);
    CHECK(tone.depth == image::OutputDepth::word);
    CHECK(plan.alpha == image::AlphaPolicy::black);
    CHECK(plan.profile == image::ProfilePolicy::srgb);
    const auto light = std::get<methods::Surface>(plan.photometric.illumination).parameters();
    CHECK(light.mode == methods::SurfaceMode::automatic);
    CHECK(light.strength == 0.25);
    CHECK(light.max_gain == 3);
    CHECK(light.target == 0.7);
    CHECK(light.cell == 32);
    CHECK(light.quantile == 0.8);
    CHECK(light.smooth == 4);
    const auto denoise = std::get<methods::Nlm>(plan.photometric.denoising).parameters();
    CHECK(denoise.h == 4);
    CHECK(denoise.patch == 5);
    CHECK(denoise.search == 9);
    CHECK(denoise.blend == 0.75);
    CHECK(err.str().empty());
}
} // namespace docenhance::tests
