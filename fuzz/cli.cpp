// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
// Fuzzing of the complete command line: argument parsing (CLI11), validation and dispatch.
// Properties: only contract exit codes; exactly one well-formed JSON object in JSON mode, with
// nothing on stderr; errors on stderr in text mode; parsed JSON flags select JSON, while
// option values keep their role; syntax failures retain raw-token fallback. Repeated runs agree.
#include "docenhance/cli/run.hpp"
#include "docenhance/contract/cli_contract.hpp"
#include "processor.hpp"
#include "stub_verifier.hpp"
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
    unsigned processor_calls;
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
    docenhance::tests::RefusingVerifier verifier;
    const int code =
        docenhance::cli::run(argv, {.processor = processor, .verifier = verifier}, out, err);
    return {.code = code, .out = out.str(), .err = err.str(), .processor_calls = processor.calls};
}

void check_json(const Outcome& outcome) {
    require(outcome.out.ends_with("}\n"), "JSON output is one object and a newline");
    require(outcome.err.empty(), "JSON mode writes nothing to stderr");
    const auto document = nlohmann::json::parse(outcome.out, nullptr, false);
    require(!document.is_discarded() && document.is_object(), "JSON output parses as an object");
    require(document.value("schema_version", 0U) == docenhance::contract::response_schema_version,
            "JSON output carries the reviewed schema version");
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
                "text errors contain a diagnostic on stderr");
    }
}
struct JsonRoles {
    bool fallback = false;
    bool flag = false;
    bool exact_flag = false;
    bool syntax_failure = false;
};
JsonRoles json_roles(const std::vector<std::string>& args) {
    JsonRoles roles;
    // Independent role walk uses the reviewed catalog, not the CLI parser. A string option's
    // next token is its value; an equals value does not consume another token. Syntax failure
    // can deliberately select JSON more broadly. Joined flags use the reviewed default spellings.
    const auto option_tokens =
        args | std::views::drop(1) |
        std::views::take_while([](const std::string& arg) { return arg != "--"; });
    roles.fallback =
        std::ranges::any_of(option_tokens, [](const std::string& arg) { return arg == "--json"; });
    bool pending_value = false;
    for (const auto& arg : args | std::views::drop(1)) {
        if (arg == "--") {
            break;
        }
        if (pending_value) {
            pending_value = false;
            continue;
        }
        if (arg == "--json") {
            roles.exact_flag = true;
            roles.flag = true;
        } else if (arg == "--json=true" || arg == "--json=" || arg == "--json={}") {
            roles.flag = true;
        } else if (arg.starts_with("--json=") || arg == "--wat") {
            // These unconsumed tokens necessarily fail syntax; admission cannot accept them.
            roles.syntax_failure = true;
        }
        pending_value =
            std::ranges::any_of(docenhance::contract::option_catalog, [&arg](const auto& option) {
                return arg == option.name && !option.metavar.empty();
            });
    }
    return roles;
}
void check_json_roles(const std::vector<std::string>& args, const Outcome& outcome) {
    const bool json_mode = outcome.out.starts_with('{');
    const auto roles = json_roles(args);
    if (roles.syntax_failure) {
        require(outcome.code == exit_invocation && json_mode == roles.fallback,
                "known syntax errors use only the exact-token JSON fallback");
    }
    // Syntax failure uses only an exact raw token. Successful syntax can also admit CLI11's
    // default-valued flag spellings; argument exit 2 alone cannot distinguish syntax/admission.
    if (roles.exact_flag || (roles.flag && outcome.code != exit_invocation)) {
        require(json_mode, "a parsed JSON flag selects JSON output");
    }
    if (json_mode) {
        require(outcome.code == exit_invocation ? (roles.flag || roles.fallback) : roles.flag,
                "JSON follows parsed flag roles or the syntax-failure fallback");
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
    require(outcome.processor_calls <= 1, "processing is invoked at most once");
    if (outcome.code != exit_processing ||
        (args.size() > 1 &&
         (args[1] == "version" || args[1] == "methods" || args[1] == "verify"))) {
        require(outcome.processor_calls == 0,
                "rejected admission and read-only commands never invoke processing");
    }
    const bool json_mode = outcome.out.starts_with('{');
    if (json_mode) {
        check_json(outcome);
    } else {
        check_text(outcome);
    }
    check_json_roles(args, outcome);
    require(invoke(args) == outcome, "the command line is deterministic");
    return 0;
}
