// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/cli/run.hpp"

#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/cli/tokenize.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/report/render.hpp"
#include "failures.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <ios>
#include <limits>
#include <new>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
namespace docenhance::cli {
app::Outcome allocation_failure(const app::Invocation& invocation) noexcept {
    static_assert(std::is_nothrow_move_constructible_v<app::Outcome>);
    // Empty owning strings do not allocate. The renderer supplies a static diagnostic later.
    return app::failure(invocation, {.code = core::ErrorCode::resource, .message = {}});
}
namespace {
using docenhance::app::Invocation;
using docenhance::app::Outcome;
using docenhance::core::ErrorCode;
Outcome argument_error(const Invocation& invocation, std::string message) {
    return app::failure(invocation, {.code = ErrorCode::argument, .message = std::move(message)});
}
Outcome parse_and_dispatch(std::span<const char* const> args, Invocation& invocation, bool& json,
                           Ports ports, const core::Cancellation& cancellation) {
    if (!core::valid_utf8(args.front())) {
        json = syntax_json(args.subspan(1));
        return argument_error(invocation, "Arguments must be well-formed UTF-8");
    }
    std::vector<std::string_view> tokens;
    tokens.reserve(args.size() - 1);
    for (const auto* const argument : args.subspan(1)) {
        tokens.emplace_back(argument);
    }
    auto parsed = tokenize(tokens);
    if (!parsed) {
        json = syntax_json(args.subspan(1));
        invocation.command = parsed.error().command;
        return argument_error(invocation, std::move(parsed.error().message));
    }
    invocation = std::move(*parsed);
    json = invocation.contains(app::OptionId::json);
    return app::dispatch(invocation, ports.processor.get(), ports.verifier.get(), cancellation);
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
Outcome contained(std::span<const char* const> args, Invocation& invocation, bool& json,
                  Ports ports, const core::Cancellation& cancellation) {
    std::optional<Outcome> outcome;
    try {
        outcome = parse_and_dispatch(args, invocation, json, ports, cancellation);
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
} // namespace
int run(std::span<const char* const> args, Ports ports, std::ostream& out, std::ostream& err,
        const core::Cancellation& cancellation) {
    if (!usable(args)) {
        return static_cast<int>(core::ExitCode::invocation);
    }
    Invocation invocation;
    bool json = false;
    const auto outcome = contained(args, invocation, json, ports, cancellation);
    // Publication may already be completed. Never retry rendering or claim it did not start.
    try {
        return emit(outcome, json, out, err);
    } catch (...) {
        return static_cast<int>(core::ExitCode::output);
    }
}
} // namespace docenhance::cli
