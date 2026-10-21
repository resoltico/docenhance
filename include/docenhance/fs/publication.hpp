// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace docenhance::fs {
// A native reserved location, owned by the transaction and used by bounded byte I/O.
struct BundleSlot;
// A file this transaction places in the bundle. The files are written in the order declared, so
// one that describes the others comes last. The caller keeps every byte owner alive until this
// synchronous transaction and its validation callbacks return.
struct BundleFile {
    std::string_view relative;
    std::span<const std::byte> bytes;
};
struct PublishFailure {
    core::Error error;
    core::Publication publication = core::Publication::not_started;
};
template <typename T> using PublishResult = std::expected<T, PublishFailure>;
// Processing meaning stays in the host. The I/O transaction invokes the supplied validator
// before and after commit; native publication ownership stays in I/O. It never parses a record.
struct BundleValidation {
    void* state = nullptr;
    core::Result<void> (*validate)(void*, const std::string&, const core::Cancellation&) = nullptr;
};
// Writes exactly these bytes into the reserved slot. No directory is created and no existing file
// is replaced: the transaction owns the staging directory.
[[nodiscard]] core::Result<void> write_bytes(const BundleSlot& slot,
                                             std::span<const std::byte> content,
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
[[nodiscard]] PublishResult<std::string> publish_bundle(const std::string& output_directory,
                                                        std::span<const BundleFile> files,
                                                        const core::Cancellation& cancellation = {},
                                                        BundleValidation validation = {});
} // namespace docenhance::fs
