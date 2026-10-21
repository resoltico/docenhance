// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/record/record.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>
namespace docenhance::record {
// One result and at most the two closed asset roles. run.json never identifies itself.
inline constexpr std::size_t record_max_artifacts = 3;
[[nodiscard]] core::Result<RunRecord> read_record(std::span<const std::byte> bytes);
[[nodiscard]] core::Result<std::vector<Artifact>> inventory(const RunRecord& value);
// Exact artifact/record agreement, never authenticity or proof of original execution.
[[nodiscard]] core::Result<void> agrees(const RunRecord& declared,
                                        std::span<const core::NamedContent> present,
                                        std::span<const std::string> directories);
} // namespace docenhance::record
