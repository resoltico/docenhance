// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
// Fuzzing of the complete command line: argument parsing (CLI11), validation and dispatch.
// Properties: only contract exit codes; exactly one well-formed JSON object in JSON mode, with
// nothing on stderr; errors on stderr in text mode; JSON errors whenever an exact --json option
// token (before any "--") is present; identical results on repeated runs.
#include "docenhance/cli/run.hpp"
#include "processor.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
namespace {
using docenhance::fuzz::require;
constexpr std::size_t max_arguments = 12;
constexpr std::size_t max_argument_length = 96;
constexpr int exit_success = 0;
constexpr int exit_invocation = 2;
constexpr int exit_input = 3;
constexpr int exit_processing = 4;
constexpr int exit_output = 5;

struct Outcome {
    int code;
    std::string out;
    std::string err;
    bool operator==(const Outcome&) const = default;
};

// Arguments are NUL-separated so that any byte except NUL can appear inside one.
std::vector<std::string> arguments(docenhance::fuzz::FuzzInput& input) {
    std::vector<std::string> args{"docenhance"};
    const auto text = input.rest(max_arguments * (max_argument_length + 1));
    std::string current;
    for (const char c : text) {
        if (c == '\0') {
            args.push_back(current);
            current.clear();
        } else if (current.size() < max_argument_length) {
            current.push_back(c);
        }
    }
    args.push_back(current);
    args.resize(std::min(args.size(), max_arguments + 1));
    return args;
}

Outcome invoke(const std::vector<std::string>& args) {
    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
    }
    std::ostringstream out;
    std::ostringstream err;
    docenhance::tests::RejectingProcessor processor;
    const int code = docenhance::cli::run(argv, processor, out, err);
    return {.code = code, .out = out.str(), .err = err.str()};
}

void check_json(const Outcome& outcome) {
    require(outcome.out.ends_with("}\n"), "JSON output is one object and a newline");
    require(outcome.err.empty(), "JSON mode writes nothing to stderr");
    const auto document = nlohmann::json::parse(outcome.out, nullptr, false);
    require(!document.is_discarded() && document.is_object(), "JSON output parses as an object");
    require(document.value("schema_version", 0) == 1, "JSON output carries schema_version 1");
    require(document.value("exit_code", -1) == outcome.code, "JSON exit_code equals the exit code");
    if (outcome.code != exit_success) {
        const auto error = document.find("error");
        require(error != document.end() && error->is_object() &&
                    error->value("code", std::string()).starts_with("E_"),
                "JSON errors carry a code");
        require(document.value("publication", std::string()) == "not_started",
                "failed commands never start publication");
    }
}

void check_text(const Outcome& outcome) {
    if (outcome.code == exit_success) {
        require(!outcome.out.empty() && outcome.err.empty(), "text success writes only stdout");
    } else {
        require(outcome.out.empty(), "text errors write nothing to stdout");
        require(outcome.err.starts_with("E_") && outcome.err.ends_with('\n'),
                "text errors are one diagnostic line on stderr");
    }
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    docenhance::fuzz::FuzzInput input(std::span<const std::uint8_t>(data, size));
    const auto args = arguments(input);
    const auto outcome = invoke(args);
    require(outcome.code == exit_success || outcome.code == exit_invocation ||
                outcome.code == exit_input || outcome.code == exit_processing ||
                outcome.code == exit_output,
            "only contract exit codes occur (invariant failures are defects)");
    const bool json_mode = outcome.out.starts_with('{');
    if (json_mode) {
        check_json(outcome);
    } else {
        check_text(outcome);
    }
    // Arguments after "--" are operands, not options.
    const auto options = args | std::views::drop(1) |
                         std::views::take_while([](const std::string& a) { return a != "--"; });
    const bool json_token =
        std::ranges::any_of(options, [](const std::string& a) { return a == "--json"; });
    if (outcome.code != exit_success && json_token) {
        require(json_mode, "an exact --json token selects JSON error output");
    }
    require(invoke(args) == outcome, "the command line is deterministic");
    return 0;
}
