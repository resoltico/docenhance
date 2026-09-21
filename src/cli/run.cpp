// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/cli/run.hpp"

#include "docenhance/app/dispatch.hpp"
#include "docenhance/contract/cli_contract.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/report/render.hpp"

#include <CLI/CLI.hpp>
#include <algorithm>
#include <array>
#include <exception>
#include <map>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
namespace docenhance::cli {
namespace {
using docenhance::app::Outcome;
using docenhance::contract::Command;
using docenhance::contract::Invocation;
using docenhance::core::ErrorCode;
// NOLINTNEXTLINE(bugprone-exception-escape): parse_and_dispatch is caught at the adapter boundary.
struct ParsedCommand {
    ParsedCommand(Command value, CLI::App* app) : command(value), parser(app) {}
    Command command;
    CLI::App* parser;
    bool json = false;
    bool help = false;
    std::string subject;
    std::map<std::string, std::string> options;
    std::map<std::string, bool> flags;
};
struct RootFlags {
    bool help = false;
    bool json = false;
    bool version = false;
};
Outcome argument_error(const Invocation& invocation, std::string message) {
    return docenhance::app::failure(invocation,
                                    {.code = ErrorCode::argument, .message = std::move(message)});
}
bool has_repeated_option(const CLI::App& app) {
    return std::ranges::any_of(app.get_options(),
                               [](const CLI::Option* option) { return option->count() > 1; });
}
void add_switches(ParsedCommand& command) {
    command.parser->set_help_flag(); // Our core, not CLI11, owns text/JSON help.
    command.parser->add_flag("--help", command.help)->disable_flag_override();
    command.parser->add_flag("--json", command.json)->disable_flag_override();
}
// The contract states which commands an option belongs to; this adapter only asks.
void add_target_options(ParsedCommand& command) {
    for (const auto& option : docenhance::contract::option_catalog) {
        if (!option.scope.contains(command.command) || option.name == "--help" ||
            option.name == "--json") {
            continue;
        }
        const std::string name(option.name);
        if (option.metavar.empty()) {
            command.parser->add_flag(name, command.flags[name], std::string(option.description))
                ->disable_flag_override();
        } else {
            command.parser->add_option(name, command.options[name], std::string(option.description))
                ->type_name(std::string(option.metavar))
                ->multi_option_policy(CLI::MultiOptionPolicy::Throw);
        }
    }
}
// Copies the parsed subcommand into the invocation and rejects flags placed before it.
std::optional<Outcome> select_command(std::span<ParsedCommand> commands, const RootFlags& root,
                                      Invocation& invocation) {
    for (auto& command : commands) {
        if (!command.parser->parsed()) {
            continue;
        }
        invocation.command = command.command;
        invocation.help = command.help;
        // Keep a --json seen by the pre-scan: errors below must still be reported as JSON.
        invocation.json = invocation.json || command.json;
        invocation.subject = std::move(command.subject);
        if (root.help || root.json || root.version) {
            return argument_error(invocation, "Root flags cannot be combined with a subcommand; "
                                              "place --help/--json after the command");
        }
        if (has_repeated_option(*command.parser)) {
            return argument_error(invocation, "Repeated options are not allowed");
        }
    }
    return std::nullopt;
}
std::optional<Outcome> apply_root_flags(const CLI::App& cli, const RootFlags& root,
                                        Invocation& invocation) {
    invocation.help = root.help;
    invocation.json = root.json;
    invocation.root_version = root.version;
    if (has_repeated_option(cli)) {
        return argument_error(invocation, "Repeated root options are not allowed");
    }
    if (root.version && (root.help || root.json)) {
        return argument_error(
            invocation, "Use 'version --json'; --version cannot be combined with other flags");
    }
    return std::nullopt;
}
std::optional<Outcome> require_target_arguments(const Invocation& invocation,
                                                const ParsedCommand& process) {
    const bool targets_input = invocation.command == Command::process ||
                               invocation.command == Command::plan ||
                               invocation.command == Command::inspect;
    if (invocation.help || !targets_input) {
        return std::nullopt;
    }
    if (invocation.subject.empty()) {
        return argument_error(invocation, "INPUT is required");
    }
    const auto out_dir = process.options.find("--out-dir");
    if (invocation.command == Command::process &&
        (out_dir == process.options.end() || out_dir->second.empty())) {
        return argument_error(invocation, "--out-dir is required");
    }
    return std::nullopt;
}
Outcome parse_and_dispatch(std::span<const char* const> args, Invocation& invocation) {
    CLI::App cli{"DocEnhance development foundation"};
    cli.set_help_flag();
    cli.require_subcommand(0, 1);
    RootFlags root;
    cli.add_flag("--help", root.help)->disable_flag_override();
    cli.add_flag("--json", root.json)->disable_flag_override();
    cli.add_flag("--version", root.version)->disable_flag_override();
    // Braced-list elements are evaluated in order, so subcommands register in this order.
    auto commands = std::to_array<ParsedCommand>({
        {Command::process, cli.add_subcommand("process")},
        {Command::plan, cli.add_subcommand("plan")},
        {Command::inspect, cli.add_subcommand("inspect")},
        {Command::presets, cli.add_subcommand("presets")},
        {Command::methods, cli.add_subcommand("methods")},
        {Command::version, cli.add_subcommand("version")},
    });
    for (auto& command : commands) {
        add_switches(command);
        if (command.command != Command::version) {
            command.parser->add_option("subject", command.subject)
                ->multi_option_policy(CLI::MultiOptionPolicy::Throw);
        }
    }
    auto& process = commands.at(0);
    add_target_options(process);
    add_target_options(commands.at(1));
    add_target_options(commands.at(2));
    cli.parse(static_cast<int>(args.size()), args.data());
    if (auto rejected = select_command(commands, root, invocation)) {
        return std::move(*rejected);
    }
    if (invocation.command == Command::root) {
        if (auto rejected = apply_root_flags(cli, root, invocation)) {
            return std::move(*rejected);
        }
    }
    if (auto rejected = require_target_arguments(invocation, process)) {
        return std::move(*rejected);
    }
    return docenhance::app::dispatch(invocation);
}
// The only place that turns an outcome into bytes, and the only place that knows the streams.
int emit(const Outcome& outcome, bool json, std::ostream& out, std::ostream& err) {
    const auto format = json ? report::Format::json : report::Format::text;
    const auto rendered = report::render(outcome, format);
    out << rendered.out;
    err << rendered.err;
    if (!out || !err) {
        return static_cast<int>(docenhance::core::ExitCode::output);
    }
    return static_cast<int>(outcome.exit_code);
}
} // namespace
int run(std::span<const char* const> args, std::ostream& out, std::ostream& err) {
    Invocation invocation;
    // Error presentation honors an exact --json token, even when parsing later fails. Arguments
    // after "--" are operands, never options.
    const auto options = args | std::views::drop(1) | std::views::take_while([](const char* arg) {
                             return std::string_view(arg) != "--";
                         });
    invocation.json = std::ranges::any_of(
        options, [](const char* arg) { return std::string_view(arg) == "--json"; });
    // The invocation's --json is read before parsing, so a parse failure is reported in the form
    // the caller asked for. Everything below reports through the same two steps: decide, render.
    try {
        const auto outcome = parse_and_dispatch(args, invocation);
        return emit(outcome, invocation.json, out, err);
    } catch (const CLI::ParseError& error) {
        return emit(argument_error(invocation, error.what()), invocation.json, out, err);
    } catch (const std::exception& error) {
        return emit(docenhance::app::failure(
                        invocation, {.code = ErrorCode::invariant, .message = error.what()}),
                    invocation.json, out, err);
    } catch (...) {
        return emit(docenhance::app::failure(invocation,
                                             {
                                                 .code = ErrorCode::invariant,
                                                 .message = "Unknown non-standard exception",
                                             }),
                    invocation.json, out, err);
    }
}
} // namespace docenhance::cli
