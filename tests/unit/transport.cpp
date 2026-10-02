// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/process.hpp"
#include "docenhance/cli/run.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
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
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const core::Cancellation& /*cancellation*/) override {
        ++calls;
        return app::PublishedBinary{.output = "result/result.png", .run = {}, .record = {}};
    }
};
class CapturingProcessor final : public app::Processor {
  public:
    std::optional<app::ProcessRequest> received;
    app::ProcessResult process(const app::ProcessRequest& request,
                               const core::Cancellation& /*cancellation*/) override {
        received = request;
        return app::process_failure({
            .code = core::ErrorCode::unavailable,
            .message = "Captured without processing effects",
        });
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
    CHECK(request.protection() == "mask.png");
    const auto tone = std::get<image::Continuous>(request.operation()).parameters();
    CHECK(tone.mode == image::ToneMode::gray);
    CHECK(tone.depth == image::OutputDepth::word);
    CHECK(tone.alpha == image::AlphaPolicy::black);
    CHECK(tone.profile == image::ProfilePolicy::srgb);
    const auto light = std::get<methods::Surface>(request.illumination()).parameters();
    CHECK(light.mode == methods::SurfaceMode::automatic);
    CHECK(light.strength == 0.25);
    CHECK(light.max_gain == 3);
    CHECK(light.target == 0.7);
    CHECK(light.cell == 32);
    CHECK(light.quantile == 0.8);
    CHECK(light.smooth == 4);
    const auto denoise = std::get<methods::Nlm>(request.denoising()).parameters();
    CHECK(denoise.h == 4);
    CHECK(denoise.patch == 5);
    CHECK(denoise.search == 9);
    CHECK(denoise.blend == 0.75);
    CHECK(err.str().empty());
}
} // namespace docenhance::tests
