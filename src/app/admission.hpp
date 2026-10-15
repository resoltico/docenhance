// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/core/result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
namespace docenhance::app {
struct Grid {
    std::uint32_t columns;
    std::uint32_t rows;
};
using SuppliedValue = std::variant<std::monostate, std::string_view, double, std::uint32_t, Grid>;
// Borrowed views live only during admission. Paths are copied into their request owners.
struct SuppliedOptions {
    std::array<SuppliedValue, option_count> values{};
    [[nodiscard]] const SuppliedValue& get(OptionId id) const {
        return values.at(static_cast<std::size_t>(id));
    }
};
[[nodiscard]] core::Result<void> validate_invocation(const Invocation& invocation);
[[nodiscard]] core::Result<SuppliedOptions> admit_process_options(const Invocation& invocation);
[[nodiscard]] std::string_view effective_value(const Invocation& invocation, OptionId id);
[[nodiscard]] std::string_view selected_method(const Invocation& invocation,
                                               const OptionDescriptor& selector);
[[nodiscard]] inline std::unexpected<core::Error> binding_error() {
    return core::failure(core::ErrorCode::invariant,
                         "Generated parameter slot does not match its declared type");
}
// Missing slots keep generated typed defaults. A present slot of a wrong type is never ignored.
template <typename T>
[[nodiscard]] bool assign_supplied(const SuppliedOptions& options, OptionId id, T& target) {
    const auto& slot = options.get(id);
    if (std::holds_alternative<std::monostate>(slot)) {
        return true;
    }
    const auto* const value = std::get_if<T>(&slot);
    if (value == nullptr) {
        return false;
    }
    target = *value;
    return true;
}
[[nodiscard]] inline bool assign_supplied(const SuppliedOptions& options, OptionId id,
                                          std::string& target) {
    const auto& slot = options.get(id);
    if (std::holds_alternative<std::monostate>(slot)) {
        return true;
    }
    const auto* const value = std::get_if<std::string_view>(&slot);
    if (value == nullptr) {
        return false;
    }
    target = *value;
    return true;
}
template <typename T>
[[nodiscard]] bool assign_supplied(const SuppliedOptions& options, OptionId id,
                                   std::optional<T>& target, std::string_view automatic) {
    const auto& slot = options.get(id);
    if (std::holds_alternative<std::monostate>(slot)) {
        return true;
    }
    if (const auto* const value = std::get_if<T>(&slot)) {
        target = *value;
        return true;
    }
    const auto* const symbolic = std::get_if<std::string_view>(&slot);
    if (symbolic == nullptr || *symbolic != automatic) {
        return false;
    }
    target.reset();
    return true;
}
[[nodiscard]] inline bool assign_grid(const SuppliedOptions& options, OptionId id,
                                      std::uint32_t& columns, std::uint32_t& rows) {
    const auto& slot = options.get(id);
    if (std::holds_alternative<std::monostate>(slot)) {
        return true;
    }
    const auto* const grid = std::get_if<Grid>(&slot);
    if (grid == nullptr) {
        return false;
    }
    columns = grid->columns;
    rows = grid->rows;
    return true;
}
} // namespace docenhance::app
