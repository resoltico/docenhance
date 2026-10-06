// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace docenhance::bundle {
// Reading a record that someone else wrote. A supplied bundle is untrusted input: it is bounded
// in size and in nesting before it is parsed at all, it is parsed without exceptions, and what
// comes back is typed. No field can name a path outside the bundle, and nothing here executes,
// reruns or interprets the request that was recorded.

// A record larger or deeper than this is refused rather than survived. The depth bound is checked
// before parsing, because a recursive-descent parser meets a deeply nested document with the
// stack, not with an error.
inline constexpr std::size_t record_max_bytes = std::size_t{1} * 1024 * 1024;
inline constexpr std::size_t record_max_depth = 16;
inline constexpr std::size_t record_max_events = 1024;
inline constexpr std::size_t record_max_artifacts = 16;

// What a record declares this bundle contains. The inventory is closed: a file that is present
// and undeclared is as much a disagreement as a declared file that is missing.
struct RecordedBuild {
    std::string version;
    std::string platform;
    std::string compiler;
    std::string dependency_lock_sha256;
};
struct DeclaredBundle {
    unsigned version = 0;
    std::string run;
    std::string recorded;
    std::vector<Artifact> inventory;
    RecordedBuild build{};
    SourceFacts source{};
    bool protection_supplied = false;
    std::optional<Operation> operation = std::nullopt;
    OutputFacts output{};
    std::optional<ProtectionFacts> protection = std::nullopt;
    std::optional<image::ConversionReport> conversion = std::nullopt;
    methods::IlluminationReport illumination{};
    methods::DenoisingReport denoising{};
    methods::ContrastReport contrast{};
};

[[nodiscard]] core::Result<DeclaredBundle> read_record(std::span<const std::byte> bytes);

// Whether what a bundle contains is what its record declares. The inventory is closed in both
// directions: a declared file that is absent and a present file that is undeclared are different
// disagreements, and neither is a pass. The record itself is expected to be present and is not
// identified against itself.
//
// Agreement is not authenticity. Digests detect disagreement with expected bytes; anyone able to
// rewrite an artifact and its record can produce another internally consistent bundle.
[[nodiscard]] core::Result<void> agrees(const DeclaredBundle& declared,
                                        std::span<const core::NamedContent> present,
                                        std::span<const std::string> directories);
} // namespace docenhance::bundle
