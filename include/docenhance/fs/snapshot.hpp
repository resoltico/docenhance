// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <string>

namespace docenhance::fs {
struct Snapshot {
    core::Buffer encoded;
    core::ContentIdentity identity;
};
// Open/read/finish one regular source, then identify those same owned bytes once. An empty
// acquired file has its real empty-byte identity; decoding, rather than readiness, refuses it.
[[nodiscard]] core::Result<Snapshot> read_source_snapshot(const std::string& input,
                                                          core::Budget& budget,
                                                          const core::Cancellation& cancellation,
                                                          std::size_t maximum_bytes);
} // namespace docenhance::fs
