// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <span>
#include <string_view>
namespace docenhance::io {
// Linked codec libraries do not imply implemented, contract-compliant decoders.
[[nodiscard]] std::span<const std::string_view> supported_input_formats() noexcept;
} // namespace docenhance::io
