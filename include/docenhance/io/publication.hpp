// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

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
// The identity of one bundle file, streamed through a fixed buffer so that identifying a large
// file costs no working budget, and refused once it passes the bound given. The size it reports is
// the number of bytes it read, so identity and size describe one reading of one file rather than a
// measurement and a later read that could disagree. Publication bounds its own output by what a
// bundle may hold, so a bundle this program writes is one it can read back.
[[nodiscard]] core::Result<core::ContentIdentity>
identify_slot(const BundleSlot& slot, std::uint64_t limit,
              const core::Cancellation& cancellation = {});
// Writes every declared file into an owned staging directory, observes the one cancellation
// cutoff, then commits the complete directory with a single exclusive rename. A failure before
// commit prevents publication; cleanup removes only what this invocation created. Returns the path
// of the first declared file, which is the result the caller asked to produce.
[[nodiscard]] core::Result<std::string> publish_bundle(const std::string& output_directory,
                                                       std::span<const BundleFile> files,
                                                       const core::Cancellation& cancellation = {},
                                                       BundleValidation validation = {});
} // namespace docenhance::io
