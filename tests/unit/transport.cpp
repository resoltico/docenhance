// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/process.hpp"
#include "docenhance/cli/run.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "stub_verifier.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <ios>
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
