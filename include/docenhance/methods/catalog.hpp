// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <span>
#include <string_view>
namespace docenhance::methods {
struct ImplementedMethod {
    std::string_view id;
    unsigned method_version;
};
// Only complete, validated algorithms belong here; the planned ones live in
// spec/method-contract.json and are not a capability until they are implemented.
[[nodiscard]] std::span<const ImplementedMethod> implemented_methods() noexcept;
} // namespace docenhance::methods
