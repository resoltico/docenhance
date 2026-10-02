// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/cli/run.hpp"

#include "docenhance/app/dispatch.hpp"
#include "docenhance/contract/cli_contract.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/contract/utf8.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/report/render.hpp"
#include "failures.hpp"

#include <CLI/CLI.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <ios>
#include <limits>
#include <new>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::cli {
app::Outcome allocation_failure(const contract::Invocation& invocation) noexcept {
    static_assert(std::is_nothrow_move_constructible_v<app::Outcome>);
    // Empty owning strings do not allocate. The renderer supplies a static diagnostic later.
    return app::failure(invocation, {.code = core::ErrorCode::resource, .message = {}});
}
namespace {
using docenhance::app::Outcome;
using docenhance::contract::Command;
using docenhance::contract::Invocation;
using docenhance::core::ErrorCode;
struct ParsedCommand {
    CLI::App* parser;
    Invocation invocation;
};
Outcome argument_error(const Invocation& invocation, std::string message) {
    return docenhance::app::failure(invocation,
                                    {.code = ErrorCode::argument, .message = std::move(message)});
}
bool has_repeated_option(const CLI::App& app) {
    return std::ranges::any_of(app.get_options(),
                               [](const CLI::Option* option) { return option->count() > 1; });
}
template <typename Member>
void bind_option(ParsedCommand& command, const docenhance::contract::OptionDescriptor& option,
                 Member member) {
    const std::string name(option.name);
    const std::string description(option.description);
    if constexpr (std::is_same_v<Member, bool Invocation::*>) {
        command.parser->add_flag(name, command.invocation.*member, description)
            ->disable_flag_override();
    } else {
        command.parser
            ->add_option_function<std::string>(
                name,
                [&command, member](const std::string& value) {
                    command.invocation.*member = value;
                },
                description)
            ->type_name(std::string(option.metavar))
            ->multi_option_policy(CLI::MultiOptionPolicy::Throw);
    }
}
// Metadata binds syntax directly to caller-owned invocation storage. Application factories still
// own all domains and cross-option meaning; this adapter preserves raw presence, including empty.
void add_options(ParsedCommand& command) {
    command.parser->set_help_flag();
    for (const auto& option : docenhance::contract::option_catalog) {
        if (option.scope.contains(command.invocation.command)) {
            std::visit([&](auto member) { bind_option(command, option, member); }, option.binding);
        }
    }
}
// Copies the parsed subcommand into the invocation and rejects flags placed before it.
std::optional<Outcome> select_command(std::span<ParsedCommand> commands, const Invocation& root,
                                      Invocation& invocation) {
    for (auto& command : commands) {
        if (!command.parser->parsed()) {
            continue;
        }
        // Keep the exact --json pre-scan even when syntax later fails.
        command.invocation.json = command.invocation.json || invocation.json;
        invocation = std::move(command.invocation);
        if (root.help || root.json || root.root_version) {
            return argument_error(invocation, "Root flags cannot be combined with a subcommand; "
                                              "place --help/--json after the command");
        }
        if (has_repeated_option(*command.parser)) {
            return argument_error(invocation, "Repeated options are not allowed");
        }
    }
    return std::nullopt;
}
std::optional<Outcome> apply_root_flags(const CLI::App& cli, Invocation root,
                                        Invocation& invocation) {
    root.json = root.json || invocation.json;
    invocation = std::move(root);
    if (has_repeated_option(cli)) {
        return argument_error(invocation, "Repeated root options are not allowed");
    }
    if (invocation.root_version && (invocation.help || invocation.json)) {
        return argument_error(
            invocation, "Use 'version --json'; --version cannot be combined with other flags");
    }
    return std::nullopt;
}
Outcome parse_and_dispatch(std::span<const char* const> args, Invocation& invocation, Ports ports,
                           const core::Cancellation& cancellation) {
    if (std::ranges::any_of(args, [](const char* arg) { return !contract::valid_utf8(arg); })) {
        return argument_error(invocation, "Arguments must be well-formed UTF-8");
    }
    CLI::App cli{"DocEnhance"};
    cli.set_help_flag();
    cli.require_subcommand(0, 1);
    ParsedCommand root{.parser = &cli, .invocation = {}};
    add_options(root);
    const auto add_command = [&cli](const char* name, Command command) {
        Invocation raw;
        raw.command = command;
        return ParsedCommand{.parser = cli.add_subcommand(name), .invocation = std::move(raw)};
    };
    auto commands = std::to_array<ParsedCommand>({
        add_command("process", Command::process),
        add_command("verify", Command::verify),
        add_command("methods", Command::methods),
        add_command("version", Command::version),
    });
    for (auto& command : commands) {
        add_options(command);
        if (command.invocation.command != Command::version) {
            command.parser->add_option("subject", command.invocation.subject)
                ->multi_option_policy(CLI::MultiOptionPolicy::Throw);
        }
    }
    cli.parse(static_cast<int>(args.size()), args.data());
    if (auto rejected = select_command(commands, root.invocation, invocation)) {
        return std::move(*rejected);
    }
    if (invocation.command == Command::root) {
        if (auto rejected = apply_root_flags(cli, std::move(root.invocation), invocation)) {
            return std::move(*rejected);
        }
    }
    return docenhance::app::dispatch(invocation, ports.processor.get(), ports.verifier.get(),
                                     cancellation);
}
// Transfer rendered bytes without inheriting an embedding caller's width/fill formatting.
// Standard stream ties and exception masks remain the caller's; only direct access is selected.
bool deliver_bytes(std::string_view bytes, std::ostream& stream) {
    if (bytes.empty()) {
        return true;
    }
    if (!std::in_range<std::streamsize>(bytes.size())) {
        return false;
    }
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    return static_cast<bool>(stream);
}
// The only place that turns an outcome into bytes, and the only place that knows the streams.
int emit(const Outcome& outcome, bool json, std::ostream& out, std::ostream& err) {
    const auto format = json ? report::Format::json : report::Format::text;
    const auto rendered = report::render(outcome, format);
    if (!deliver_bytes(rendered.out, out) || !deliver_bytes(rendered.err, err)) {
        return static_cast<int>(docenhance::core::ExitCode::output);
    }
    return static_cast<int>(outcome.exit_code());
}
// Everything the adapter can meet before a response exists, turned into one outcome. The CLI is
// the single boundary an exception crosses; below it every failure is already a value.
Outcome diagnostic_failure(const Invocation& invocation, ErrorCode code, const char* const text) {
    try {
        return app::failure(invocation, {.code = code, .message = text});
    } catch (const std::bad_alloc&) {
        return allocation_failure(invocation);
    }
}
Outcome contained(std::span<const char* const> args, Invocation& invocation, Ports ports,
                  const core::Cancellation& cancellation) {
    std::optional<Outcome> outcome;
    try {
        outcome = parse_and_dispatch(args, invocation, ports, cancellation);
    } catch (const CLI::ParseError& error) {
        outcome = diagnostic_failure(invocation, ErrorCode::argument, error.what());
    } catch (const std::bad_alloc&) {
        outcome = allocation_failure(invocation);
    } catch (const std::exception& error) {
        outcome = diagnostic_failure(invocation, ErrorCode::invariant, error.what());
    } catch (...) {
        outcome =
            diagnostic_failure(invocation, ErrorCode::invariant, "Unknown non-standard exception");
    }
    return std::move(*outcome);
}
// An argument vector this adapter can work with at all: caller-owned, countable, no null entry.
bool usable(std::span<const char* const> args) {
    return !args.empty() &&
           args.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
           std::ranges::none_of(args, [](const char* arg) { return arg == nullptr; });
}
// Error presentation honours an exact --json token even when parsing later fails. Arguments after
// "--" are operands, never options.
bool asked_for_json(std::span<const char* const> args) {
    const auto options = args | std::views::drop(1) | std::views::take_while([](const char* arg) {
                             return std::string_view(arg) != "--";
                         });
    return std::ranges::any_of(options,
                               [](const char* arg) { return std::string_view(arg) == "--json"; });
}
} // namespace
int run(std::span<const char* const> args, Ports ports, std::ostream& out, std::ostream& err,
        const core::Cancellation& cancellation) {
    if (!usable(args)) {
        return static_cast<int>(core::ExitCode::invocation);
    }
    Invocation invocation;
    invocation.json = asked_for_json(args);
    const auto outcome = contained(args, invocation, ports, cancellation);
    // Publication may already be completed. Never retry rendering or claim it did not start.
    try {
        return emit(outcome, invocation.json, out, err);
    } catch (...) {
        return static_cast<int>(core::ExitCode::output);
    }
}
} // namespace docenhance::cli
