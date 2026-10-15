// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/options.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
namespace docenhance::app {
// Syntax owns raw presence and token roles. Defaults and semantic admission come afterwards.
struct Invocation {
    Command command = Command::root;
    std::vector<std::string> operands;
    std::array<std::optional<std::string>, option_count> options{};
    [[nodiscard]] const std::optional<std::string>& option(OptionId id) const {
        return options.at(static_cast<std::size_t>(id));
    }
    [[nodiscard]] std::optional<std::string>& option(OptionId id) {
        return options.at(static_cast<std::size_t>(id));
    }
    [[nodiscard]] bool contains(OptionId id) const {
        return option(id).has_value();
    }
};
} // namespace docenhance::app
