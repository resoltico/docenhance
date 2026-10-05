// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/artifact_limits.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace docenhance::io {
// Every regular file is named relative to the root and identified. Directories are separate;
// the record permits exactly the directories its paths imply. Native readers refuse links and
// special entries rather than following them, and bound the entire directory walk.
struct BundleContents {
    std::vector<core::NamedContent> files;
    std::vector<std::string> directories;
};
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
} // namespace docenhance::io
