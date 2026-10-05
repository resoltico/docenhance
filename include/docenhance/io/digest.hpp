// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <span>

namespace docenhance::io {
// The identity of content in hand. Taken here because this is where the bytes are: a path hashed
// afterwards can describe a different file than the one that was read or written.
[[nodiscard]] core::Result<core::ContentIdentity>
identify(std::span<const std::byte> content, const core::Cancellation& cancellation = {});
} // namespace docenhance::io
