// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <string_view>
namespace docenhance::contract {
enum class Command : unsigned { root, process, plan, inspect, presets, methods, version };
inline constexpr unsigned command_count = 7;
// Which commands an option belongs to, as a typed set rather than a scope string every caller has
// to interpret. spec/cli-contract.json states the scope; tools/generate_spec.py builds the sets.
class CommandSet {
  public:
    constexpr CommandSet() noexcept = default;
    template <typename... Commands>
    constexpr explicit CommandSet(Commands... commands) noexcept
        : bits_((0U | ... | bit(commands))) {}
    [[nodiscard]] constexpr bool contains(Command command) const noexcept {
        return (bits_ & bit(command)) != 0U;
    }
    [[nodiscard]] static constexpr CommandSet all() noexcept {
        CommandSet set;
        set.bits_ = (1U << command_count) - 1U;
        return set;
    }

  private:
    [[nodiscard]] static constexpr unsigned bit(Command command) noexcept {
        return 1U << static_cast<unsigned>(command);
    }
    unsigned bits_ = 0;
};
struct Invocation {
    Command command = Command::root;
    bool json = false;
    bool help = false;
    bool root_version = false;
    std::string subject;
};
[[nodiscard]] constexpr std::string_view command_name(Command command) noexcept {
    switch (command) {
    case Command::root:
        return "root";
    case Command::process:
        return "process";
    case Command::plan:
        return "plan";
    case Command::inspect:
        return "inspect";
    case Command::presets:
        return "presets";
    case Command::methods:
        return "methods";
    case Command::version:
        return "version";
    }
    return "root";
}
} // namespace docenhance::contract
