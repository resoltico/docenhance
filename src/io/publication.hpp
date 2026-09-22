// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <system_error>

namespace docenhance::io {
// A single native operation. Unsupported filesystems fail closed, never check-then-rename.
[[nodiscard]] std::error_code rename_exclusive(const std::filesystem::path& source,
                                               const std::filesystem::path& target) noexcept;
[[nodiscard]] bool definitely_not_published(const std::error_code& error) noexcept;
} // namespace docenhance::io
