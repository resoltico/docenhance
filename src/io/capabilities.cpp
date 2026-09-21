// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/io/capabilities.hpp"

#include <span>
#include <string_view>
namespace docenhance::io {
std::span<const std::string_view> supported_input_formats() noexcept {
    return {};
}
} // namespace docenhance::io
