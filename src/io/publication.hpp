// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
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
// Borrowed writer operation: encoding and verification run before the one commit cutoff.
struct PngWriterRef {
    void* state;
    core::Result<void> (*write)(void*, const std::filesystem::path&);
};
// A file this transaction places in the bundle. The owner writes it into the path supplied, and
// the files are written in the order declared, so one that describes the others comes last.
struct BundleFile {
    std::string_view relative;
    void* state;
    core::Result<void> (*write)(void*, const std::filesystem::path&);
};
// Writes every file into an owned staging directory, observes the one cancellation cutoff, and
// commits the complete directory with a single exclusive rename. A failure anywhere publishes
// nothing, and cleanup removes only what this invocation created.
[[nodiscard]] core::Result<std::string> publish_bundle(const std::string& output_directory,
                                                       std::span<const BundleFile> files,
                                                       const core::Cancellation& cancellation,
                                                       PublishRename commit);
[[nodiscard]] core::Result<std::string>
publish_generated_png(const std::string& output_directory, PngWriterRef writer,
                      const core::Cancellation& cancellation, PublishRename commit);
[[nodiscard]] core::Result<std::string>
publish_png(const std::string& output_directory, image::PlaneView<const std::uint8_t> image,
            core::Budget& budget, const core::Cancellation& cancellation, PublishRename commit);
} // namespace docenhance::io
