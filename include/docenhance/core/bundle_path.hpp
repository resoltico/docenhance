// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/utf8.hpp"

#include <cstddef>
#include <string_view>

namespace docenhance::core {
inline constexpr std::size_t bundle_path_max_bytes = 128;
// Portable artifact names, distinct from admitted native directory paths. Preserve all bytes.
[[nodiscard]] constexpr bool valid_bundle_path(std::string_view name) noexcept {
    if (name.size() > bundle_path_max_bytes || !valid_path(name) || name.front() == '/' ||
        name.contains('\\') || name.contains(':')) {
        return false;
    }
    while (!name.empty()) {
        const auto slash = name.find('/');
        auto part = name;
        if (slash != std::string_view::npos) {
            part.remove_suffix(name.size() - slash);
        }
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (slash == std::string_view::npos) {
            return true;
        }
        name.remove_prefix(slash + 1);
    }
    return false;
}
} // namespace docenhance::core
