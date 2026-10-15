// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "admission.hpp"

#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/values.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/methods/catalog.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
namespace docenhance::app {
std::string_view effective_value(const Invocation& invocation, OptionId id) {
    const auto& raw = invocation.option(id);
    return raw ? std::string_view{*raw}
               : option_catalog.at(static_cast<std::size_t>(id)).default_value.value_or("");
}
core::Result<void> validate_invocation(const Invocation& invocation) {
    if (!CommandSet::all().contains(invocation.command)) {
        return core::failure(core::ErrorCode::argument, "Unknown command value");
    }
    if (invocation.operands.size() > command_operands(invocation.command).maximum) {
        return core::failure(core::ErrorCode::argument, "Unexpected additional operand");
    }
    for (const auto& operand : invocation.operands) {
        if (!core::valid_utf8(operand)) {
            return core::failure(core::ErrorCode::argument, "Arguments must be well-formed UTF-8");
        }
    }
    for (const auto& option : option_catalog) {
        const auto& raw = invocation.option(option.id);
        if (!raw) {
            continue;
        }
        if (!option.scope.contains(invocation.command) || !core::valid_utf8(*raw) ||
            (option.kind == ValueKind::flag && !raw->empty())) {
            return std::unexpected(option_error(
                {.name = option.name, .value = *raw, .reason = "Invalid option syntax or scope"}));
        }
    }
    if (invocation.command == Command::root && invocation.contains(OptionId::version) &&
        (invocation.contains(OptionId::help) || invocation.contains(OptionId::json))) {
        return core::failure(core::ErrorCode::argument, "--version must be used alone");
    }
    return {};
}
namespace {
core::Result<SuppliedValue> parse_text(const OptionDescriptor& option, std::string_view raw) {
    if ((option.kind == ValueKind::path && !core::valid_path(raw)) ||
        (option.kind == ValueKind::choice &&
         std::ranges::find(option.choices, raw) == option.choices.end())) {
        return std::unexpected(option_error(
            {.name = option.name, .value = raw, .reason = "Value outside the permitted domain"}));
    }
    return SuppliedValue{raw};
}
core::Result<SuppliedValue> parse_grid(const OptionDescriptor& option, std::string_view raw) {
    const auto split = raw.find('x');
    if (split == std::string_view::npos) {
        return std::unexpected(
            option_error({.name = option.name, .value = raw, .reason = "Expected columns x rows"}));
    }
    const auto minimum = static_cast<std::uint32_t>(option.minimum);
    const auto maximum = static_cast<std::uint32_t>(option.maximum);
    auto columns = parse_integer_option(option.name, raw.substr(0, split), minimum, maximum);
    if (!columns) {
        return std::unexpected(std::move(columns.error()));
    }
    auto rows = parse_integer_option(option.name, raw.substr(split + 1), minimum, maximum);
    if (!rows) {
        return std::unexpected(std::move(rows.error()));
    }
    return SuppliedValue{Grid{.columns = *columns, .rows = *rows}};
}
core::Result<SuppliedValue> parse_supplied(const OptionDescriptor& option, std::string_view raw) {
    switch (option.kind) {
    case ValueKind::flag:
    case ValueKind::path:
    case ValueKind::choice:
        return parse_text(option, raw);
    case ValueKind::source_or_decimal:
        if (raw == "source") {
            return SuppliedValue{raw};
        }
        [[fallthrough]];
    case ValueKind::decimal:
        return parse_decimal_option(option.name, raw, option.minimum, option.maximum)
            .transform([](double value) -> SuppliedValue { return value; });
    case ValueKind::auto_or_integer:
        if (raw == "auto") {
            return SuppliedValue{raw};
        }
        [[fallthrough]];
    case ValueKind::integer:
    case ValueKind::odd_integer:
        return parse_integer_option(option.name, raw, static_cast<std::uint32_t>(option.minimum),
                                    static_cast<std::uint32_t>(option.maximum),
                                    option.kind == ValueKind::odd_integer)
            .transform([](std::uint32_t value) -> SuppliedValue { return value; });
    case ValueKind::grid:
        return parse_grid(option, raw);
    }
    return core::failure(core::ErrorCode::invariant, "Unknown generated option kind");
}
bool mode_applies(const Invocation& invocation, const OptionDescriptor& option) {
    return option.applies_to.empty() ||
           std::ranges::find(option.applies_to,
                             effective_value(invocation, OptionId::output_mode)) !=
               option.applies_to.end();
}
const OptionDescriptor* family_selector(std::string_view family) {
    for (const auto& option : option_catalog) {
        if (option.selector && option.family == family) {
            return &option;
        }
    }
    return nullptr;
}
bool family_applies(const Invocation& invocation, const OptionDescriptor& option) {
    if (option.family.empty() || option.selector) {
        return true;
    }
    const auto* const selector = family_selector(option.family);
    if (selector == nullptr) {
        return false;
    }
    const auto value = effective_value(invocation, selector->id);
    return std::ranges::any_of(methods::implemented_methods(), [&](const auto& method) {
        return method.family == option.family &&
               std::ranges::find(option.method_ids, method.id) != option.method_ids.end() &&
               std::ranges::find(method.selectors, value) != method.selectors.end();
    });
}
bool applicable(const Invocation& invocation, const OptionDescriptor& option) {
    if (!mode_applies(invocation, option) || !family_applies(invocation, option)) {
        return false;
    }
    return std::ranges::all_of(option.when, [&invocation](const auto& condition) {
        return effective_value(invocation, condition.option) == condition.value;
    });
}
void append_alternatives(std::string& text, std::span<const std::string_view> values) {
    bool first = true;
    for (const auto value : values) {
        if (!first) {
            text += " or ";
        }
        text += value;
        first = false;
    }
}
std::string family_requirement(const OptionDescriptor& option) {
    const auto* const selector = family_selector(option.family);
    if (selector == nullptr) {
        return "an applicable method";
    }
    std::string reason = std::string(selector->name) + " ";
    bool first = true;
    for (const auto& method : methods::implemented_methods()) {
        if (std::ranges::find(option.method_ids, method.id) == option.method_ids.end()) {
            continue;
        }
        if (!first) {
            reason += " or ";
        }
        append_alternatives(reason, method.selectors);
        first = false;
    }
    return reason;
}
std::string applicability_reason(const Invocation& invocation, const OptionDescriptor& option) {
    std::string reason = "Option requires ";
    bool first = true;
    if (!mode_applies(invocation, option)) {
        reason += "--output-mode ";
        append_alternatives(reason, option.applies_to);
        first = false;
    }
    if (!family_applies(invocation, option)) {
        reason += first ? "" : "; ";
        reason += family_requirement(option);
        first = false;
    }
    for (const auto& condition : option.when) {
        if (effective_value(invocation, condition.option) != condition.value) {
            reason += first ? "" : "; ";
            reason += option_catalog.at(static_cast<std::size_t>(condition.option)).name;
            reason += " " + std::string(condition.value);
            first = false;
        }
    }
    return reason;
}
} // namespace
std::string_view selected_method(const Invocation& invocation, const OptionDescriptor& selector) {
    if (!mode_applies(invocation, selector)) {
        return {};
    }
    const auto value = effective_value(invocation, selector.id);
    for (const auto& method : methods::implemented_methods()) {
        if (method.family == selector.family &&
            std::ranges::find(method.selectors, value) != method.selectors.end()) {
            return method.id;
        }
    }
    return {};
}
core::Result<SuppliedOptions> admit_process_options(const Invocation& invocation) {
    if (auto checked = validate_invocation(invocation); !checked) {
        return std::unexpected(std::move(checked.error()));
    }
    if (invocation.operands.size() < command_operands(invocation.command).minimum) {
        return core::failure(core::ErrorCode::argument, "Missing required operand");
    }
    SuppliedOptions parsed;
    for (const auto& option : option_catalog) {
        if (const auto& raw = invocation.option(option.id)) {
            auto value = parse_supplied(option, *raw);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            parsed.values.at(static_cast<std::size_t>(option.id)) = *value;
        }
    }
    for (const auto& option : option_catalog) {
        if (!option.scope.contains(invocation.command)) {
            continue;
        }
        const bool active = applicable(invocation, option);
        const auto& raw = invocation.option(option.id);
        if (raw && !active) {
            return std::unexpected(option_error({
                .name = option.name,
                .value = *raw,
                .reason = applicability_reason(invocation, option),
            }));
        }
        if (!raw && active && option.required) {
            return std::unexpected(option_error(
                {.name = option.name, .value = {}, .reason = "Explicit presence is required"}));
        }
    }
    return parsed;
}
} // namespace docenhance::app
