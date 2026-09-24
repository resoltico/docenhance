// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/cli/run.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/report/render.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <ios>
#include <new>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace docenhance::tests {
namespace {
class RecordingBuffer final : public std::stringbuf {
  public:
    bool fail_sync = false;
    bool throw_sync = false;
    unsigned synchronizations = 0;
    unsigned writes = 0;

  protected:
    std::streamsize xsputn(const char* text, std::streamsize size) override {
        ++writes;
        return std::stringbuf::xsputn(text, size);
    }
    int sync() override {
        ++synchronizations;
        if (throw_sync) {
            throw std::runtime_error("Downstream flush failed");
        }
        return fail_sync ? -1 : 0;
    }
};
enum class Behavior {
    success,
    standard_exception,
    allocation_exception,
    unknown_exception,
    refusal,
};
class RecordingProcessor final : public app::Processor {
  public:
    Behavior behavior = Behavior::success;
    unsigned calls = 0;
    bool effect_observed = false;
    core::Result<app::PublishedImage> process(const app::ProcessRequest& /*request*/,
                                              const core::Cancellation& /*cancellation*/) override {
        ++calls;
        if (behavior == Behavior::refusal) {
            return std::unexpected(core::Error{
                .code = core::ErrorCode::resource,
                .message = "Reported resource refusal",
                .publication = core::Publication::not_published,
            });
        }
        effect_observed = true;
        switch (behavior) {
        case Behavior::standard_exception:
            throw std::runtime_error("An effect already occurred");
        case Behavior::allocation_exception:
            throw std::bad_alloc{};
        case Behavior::unknown_exception:
            throw 42; // NOLINT(bugprone-std-exception-baseclass): Exercise catch-all containment.
        default:
            return app::PublishedImage{.output = "result/result.png"};
        }
    }
};
constexpr auto process_args = std::to_array<const char*>({
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
contract::Invocation request() {
    return {
        .command = contract::Command::process,
        .subject = "input.png",
        .output_directory = "result",
        .output_mode = "bw",
        .binarize = "fixed",
        .fixed_threshold = {},
    };
}
} // namespace

TEST_CASE("Response delivery flushes only its owned stream") {
    RecordingProcessor processor;
    RecordingBuffer output;
    RecordingBuffer diagnostic;
    std::ostream out{&output};
    std::ostream err{&diagnostic};
    REQUIRE(cli::run(process_args, processor, out, err) == 0);
    REQUIRE(processor.calls == 1);
    REQUIRE(output.synchronizations == 1);
    REQUIRE(diagnostic.writes == 0);
    REQUIRE(diagnostic.synchronizations == 0);
}
TEST_CASE("Response bytes ignore and preserve caller formatting state") {
    RecordingProcessor processor;
    std::ostringstream out;
    std::ostringstream err;
    constexpr std::streamsize field_width = 4096;
    out.fill('x');
    out.width(field_width);
    REQUIRE(cli::run(process_args, processor, out, err) == 0);
    REQUIRE(out.str().starts_with('{'));
    REQUIRE(out.str().ends_with("}\n"));
    REQUIRE(out.width() == field_width);
    REQUIRE(out.fill() == 'x');
    REQUIRE(processor.calls == 1);
    REQUIRE(err.str().empty());
}
TEST_CASE("Delayed and throwing flush failures never rerun processing") {
    for (const bool exceptions : std::array{false, true}) {
        for (const bool throwing : std::array{false, true}) {
            RecordingProcessor processor;
            RecordingBuffer output;
            output.fail_sync = true;
            output.throw_sync = throwing;
            RecordingBuffer diagnostic;
            std::ostream out{&output};
            std::ostream err{&diagnostic};
            if (exceptions) {
                out.exceptions(std::ios::badbit);
            }
            REQUIRE(cli::run(process_args, processor, out, err) == 5);
            REQUIRE(processor.calls == 1);
            REQUIRE(processor.effect_observed);
            REQUIRE(output.writes == 1);
            REQUIRE(output.synchronizations == 1);
            REQUIRE(output.str().contains("\"exit_code\": 0"));
            REQUIRE(output.str().contains("\"publication\": \"completed\""));
            REQUIRE(diagnostic.str().empty());
            REQUIRE(diagnostic.writes == 0);
            REQUIRE(diagnostic.synchronizations == 0);
        }
    }
}
TEST_CASE("An unusable unselected stream is irrelevant") {
    RecordingProcessor processor;
    RecordingBuffer output;
    RecordingBuffer diagnostic;
    std::ostream out{&output};
    std::ostream err{&diagnostic};
    err.setstate(std::ios::badbit);
    REQUIRE(cli::run(process_args, processor, out, err) == 0);
    REQUIRE(diagnostic.writes == 0);
    REQUIRE(diagnostic.synchronizations == 0);
    err.clear();
    out.setstate(std::ios::badbit);
    const auto rejected = std::to_array<const char*>({"docenhance", "unknown-command"});
    REQUIRE(cli::run(rejected, processor, out, err) == 2);
    REQUIRE(processor.calls == 1);
    REQUIRE(output.writes == 1);
    REQUIRE(output.synchronizations == 1);
    REQUIRE(diagnostic.synchronizations == 1);
}
TEST_CASE("An unreported processing effect has unknown publication") {
    for (const auto behavior : std::array{
             Behavior::standard_exception,
             Behavior::allocation_exception,
             Behavior::unknown_exception,
         }) {
        RecordingProcessor processor;
        processor.behavior = behavior;
        const auto outcome = app::dispatch(request(), processor);
        REQUIRE(processor.calls == 1);
        REQUIRE(processor.effect_observed);
        REQUIRE(outcome.exit_code() == core::ExitCode::publication_unknown);
        REQUIRE(std::get<app::Failure>(outcome.payload).error.publication ==
                core::Publication::unknown);
        std::ostringstream out;
        std::ostringstream err;
        REQUIRE(cli::run(process_args, processor, out, err) == 7);
        REQUIRE(processor.calls == 2);
        REQUIRE(out.str().contains("E_PUBLICATION_UNKNOWN"));
        REQUIRE(!out.str().contains("not_started"));
        REQUIRE(err.str().empty());
    }
}
TEST_CASE("Reported processing errors retain their known publication state") {
    RecordingProcessor processor;
    processor.behavior = Behavior::refusal;
    const auto outcome = app::dispatch(request(), processor);
    REQUIRE(outcome.exit_code() == core::ExitCode::processing);
    const auto& error = std::get<app::Failure>(outcome.payload).error;
    REQUIRE(error.publication == core::Publication::not_published);
    REQUIRE(error.message == "Reported resource refusal");
}
TEST_CASE("Malformed UTF-8 cannot cross command or direct application admission") {
    const std::string malformed(1, static_cast<char>(0xff));
    RecordingProcessor processor;
    auto args = process_args;
    args.at(2) = malformed.c_str();
    std::ostringstream out;
    std::ostringstream err;
    REQUIRE(cli::run(args, processor, out, err) == 2);
    REQUIRE(processor.calls == 0);
    REQUIRE(out.str().contains("E_ARGUMENT"));
    REQUIRE(err.str().empty());
    for (const auto& invalid : std::array{malformed, std::string{"prefix\0suffix", 13}}) {
        for (const bool input : std::array{false, true}) {
            auto invocation = request();
            (input ? invocation.subject : invocation.output_directory) = invalid;
            const auto outcome = app::dispatch(invocation, processor);
            REQUIRE(outcome.exit_code() == core::ExitCode::invocation);
            REQUIRE(std::get<app::Failure>(outcome.payload).error.publication ==
                    core::Publication::not_started);
            REQUIRE(processor.calls == 0);
        }
    }
}
TEST_CASE("Diagnostic replacement cannot rewrite a machine-readable identity") {
    const std::string malformed(1, static_cast<char>(0xff));
    const auto failure =
        app::failure(request(), {.code = core::ErrorCode::input, .message = malformed});
    for (const auto format : std::array{report::Format::json, report::Format::text}) {
        const auto rendered = report::render(failure, format);
        REQUIRE((rendered.out + rendered.err).contains("The diagnostic was not well-formed UTF-8"));
    }
    RecordingProcessor processor;
    auto outcome = app::dispatch(request(), processor);
    std::get<app::Processed>(outcome.payload).output = malformed;
    REQUIRE_THROWS(static_cast<void>(report::render(outcome, report::Format::json)));
}
} // namespace docenhance::tests
