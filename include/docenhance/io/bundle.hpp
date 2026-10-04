// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace docenhance::io {
// Where a bundle file goes. The transaction owns the location; a writer receives this and hands
// it back to the io calls below, so orchestration never has to name a filesystem type.
struct BundleSlot;
// A file this transaction places in the bundle. The files are written in the order declared, so
// one that describes the others comes last.
struct BundleFile {
    std::string_view relative;
    void* state;
    core::Result<void> (*write)(void*, const BundleSlot&);
};
// Processing meaning stays in the host. The I/O transaction invokes the supplied validator
// before and after commit; native publication ownership stays in I/O. It never parses a record.
struct BundleValidation {
    void* state = nullptr;
    core::Result<void> (*validate)(void*, const std::string&, const core::Cancellation&) = nullptr;
};
// Writes exactly these bytes into the reserved slot. No directory is created and no existing file
// is replaced: the transaction owns the staging directory.
[[nodiscard]] core::Result<void> write_bytes(const BundleSlot& slot, std::string_view content,
                                             const core::Cancellation& cancellation = {});
// The name a record keeps for a file: the final component, without the directories that led to
// it. An absolute path is never recorded, and a name can still carry personal information, so a
// bundle is not described as anonymized.
[[nodiscard]] std::string file_name(const std::string& path);

// Reading a bundle somebody else wrote. Every entry is inspected without following it: a symbolic
// link, a directory where a file belongs, or any other special entry is a refusal rather than
// something to resolve. The walk is bounded, because an unbounded directory is itself a refusal.
inline constexpr std::size_t bundle_max_entries = 64;
inline constexpr std::size_t bundle_max_file_bytes = std::size_t{256} * 1024 * 1024;
// Every regular file in the bundle, named relative to its root and identified. Directories are
// reported separately, because a record permits exactly the ones its declared paths imply.
inline constexpr std::size_t bundle_snapshot_budget = std::size_t{1} * 1024 * 1024 * 1024;
struct BundleContents {
    std::vector<core::NamedContent> files;
    std::vector<std::string> directories;
};
struct PngArtifact {
    image::RasterShape shape;
    bool profile_embedded = false;
    core::Buffer profile;
    std::optional<image::Resolution> resolution;
    bool binary_samples = true;
    bool mask_samples = true;
    std::uint64_t protected_samples = 0;
};
[[nodiscard]] core::Result<PngArtifact> observe_bundle_png(std::span<const std::byte> bytes,
                                                           core::Budget& budget,
                                                           const core::Cancellation& cancellation);
// All observations describe these immutable snapshots, never a later pathname reopening.
struct BundleSnapshot {
    core::Buffer record;
    core::Buffer image;
    core::Buffer mask;
    BundleContents contents;
};
// The host supplies the record grammar's byte bound; I/O supplies the artifact bound.
[[nodiscard]] core::Result<BundleSnapshot> read_bundle(const std::string& directory,
                                                       core::Budget& budget,
                                                       const core::Cancellation& cancellation,
                                                       std::size_t record_limit);
// Writes every declared file into an owned staging directory, observes the one cancellation
// cutoff, then commits the complete directory with a single exclusive rename. A failure before
// commit prevents publication; cleanup removes only what this invocation created. Returns the path
// of the first declared file, which is the result the caller asked to produce.
[[nodiscard]] core::Result<std::string> publish_bundle(const std::string& output_directory,
                                                       std::span<const BundleFile> files,
                                                       const core::Cancellation& cancellation = {},
                                                       BundleValidation validation = {});
} // namespace docenhance::io
