// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/methods/catalog.hpp"

#include <array>
#include <span>
namespace docenhance::methods {
std::span<const ImplementedMethod> implemented_methods() noexcept {
    static constexpr auto methods =
        std::to_array<ImplementedMethod>({{.id = "B03", .method_version = 1U}});
    return methods;
}
} // namespace docenhance::methods
