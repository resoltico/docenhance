// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace docenhance::io {
// A single native operation. Unsupported filesystems fail closed, never check-then-rename.
[[nodiscard]] std::error_code rename_exclusive(const std::filesystem::path& source,
                                               const std::filesystem::path& target) noexcept;
[[nodiscard]] bool definitely_not_published(const std::error_code& error) noexcept;
// Private operation seam: the public publisher always supplies rename_exclusive. Tests can
// coordinate cancellation after the gate and call the actual native operation without timing races.
using PublishRename = std::error_code (*)(const std::filesystem::path&,
                                          const std::filesystem::path&) noexcept;
[[nodiscard]] core::Result<std::string>
publish_png(const std::string& output_directory, image::PlaneView<const std::uint8_t> image,
            core::Budget& budget, const core::Cancellation& cancellation, PublishRename commit);
} // namespace docenhance::io
