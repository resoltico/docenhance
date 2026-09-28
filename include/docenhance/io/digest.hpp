// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/bundle.hpp"

#include <cstddef>
#include <span>

namespace docenhance::io {
// The identity of content in hand. Taken here because this is where the bytes are: a path hashed
// afterwards can describe a different file than the one that was read or written.
[[nodiscard]] core::Result<core::ContentIdentity> identify(std::span<const std::byte> content);
// The identity of a bundle file this program wrote, streamed through a fixed buffer so that
// identifying a large output costs no working budget. Used after verification, so a digest never
// describes bytes that were not checked.
[[nodiscard]] core::Result<core::ContentIdentity> identify_slot(const BundleSlot& slot);
} // namespace docenhance::io
