// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/bundle.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace docenhance::io {
// The identity of content in hand. Taken here because this is where the bytes are: a path hashed
// afterwards can describe a different file than the one that was read or written.
[[nodiscard]] core::Result<core::ContentIdentity>
identify(std::span<const std::byte> content, const core::Cancellation& cancellation = {});
// The identity of one bundle file, streamed through a fixed buffer so that identifying a large
// file costs no working budget, and refused once it passes the bound given. The size it reports is
// the number of bytes it read, so identity and size describe one reading of one file rather than a
// measurement and a later read that could disagree. Publication bounds its own output by what a
// bundle may hold, so a bundle this program writes is one it can read back.
[[nodiscard]] core::Result<core::ContentIdentity>
identify_slot(const BundleSlot& slot, std::uint64_t limit,
              const core::Cancellation& cancellation = {});
} // namespace docenhance::io
