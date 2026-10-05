// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/publication.hpp"
#include "entry_identity.hpp"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace docenhance::io {
// The reserved place a bundle file occupies while the transaction owns it.
struct BundleSlot {
    std::filesystem::path path;
    EntryLease* created = nullptr;
    std::optional<EntryIdentity> parent;
    [[nodiscard]] bool valid_parent() const {
        return parent && entry_identity(path.parent_path()) == parent;
    }
    [[nodiscard]] bool owned_file() const {
        return valid_parent() && created != nullptr && created->matches(path);
    }
};
// Private stream-operation seam for deterministic write, flush and close failure tests.
// A close operation consumes the stream even when it reports an error.
struct BundleStream {
    std::size_t (*write)(const void*, std::size_t, std::size_t, std::FILE*) = std::fwrite;
    int (*flush)(std::FILE*) = std::fflush;
    int (*close)(std::FILE*) = std::fclose;
};
[[nodiscard]] core::Result<void> write_bytes(const BundleSlot& slot, std::string_view content,
                                             const core::Cancellation& cancellation,
                                             BundleStream operations);
// A single native operation. Unsupported filesystems fail closed, never check-then-rename.
[[nodiscard]] std::error_code rename_exclusive(const std::filesystem::path& source,
                                               const std::filesystem::path& target) noexcept;
[[nodiscard]] bool definitely_not_published(const std::error_code& error) noexcept;
// Private operation seam: the public publisher always supplies rename_exclusive. Tests can
// coordinate cancellation after the gate and call the actual native operation without timing races.
using PublishRename = std::error_code (*)(const std::filesystem::path&,
                                          const std::filesystem::path&) noexcept;
// The same transaction with the commit operation supplied, so tests can coordinate cancellation
// around the native call without timing races.
[[nodiscard]] core::Result<std::string> publish_bundle(const std::string& output_directory,
                                                       std::span<const BundleFile> files,
                                                       const core::Cancellation& cancellation,
                                                       PublishRename commit,
                                                       BundleValidation validation = {});
} // namespace docenhance::io
