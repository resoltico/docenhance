// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/record/inventory.hpp"

#include "docenhance/core/bundle_path.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/record.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
namespace docenhance::record {
namespace {
core::Result<void> refusal(std::string detail) {
    return core::failure(core::ErrorCode::input, "The run record " + std::move(detail));
}
bool implied_directory(std::span<const Artifact> declared, std::string_view name) {
    return std::ranges::any_of(declared, [name](const Artifact& artifact) {
        return artifact.name.size() > name.size() && artifact.name.starts_with(name) &&
               artifact.name.at(name.size()) == '/';
    });
}
bool valid_present(std::span<const core::NamedContent> files) {
    if (files.size() > record_max_artifacts + 1) {
        return false;
    }
    auto prior = files.first(0);
    for (const auto& file : files) {
        const auto limit =
            file.name == record_name ? record_max_bytes : core::bundle_max_file_bytes;
        if (!core::valid_bundle_path(file.name) || file.identity.bytes == 0 ||
            file.identity.bytes > limit ||
            !core::valid_hexadecimal(file.identity.sha256, core::sha256_hex_length)) {
            return false;
        }
        if (std::ranges::any_of(
                prior, [&file](const auto& earlier) { return earlier.name == file.name; })) {
            return false;
        }
        prior = files.first(prior.size() + 1);
    }
    return std::ranges::any_of(files, [](const auto& file) { return file.name == record_name; });
}
} // namespace
core::Result<std::vector<Artifact>> inventory(const RunRecord& value) {
    try {
        if (!validate(value)) {
            return core::failure(core::ErrorCode::input,
                                 "The run record contains inconsistent facts");
        }
        std::vector<Artifact> result;
        result.push_back({
            .name = image_name,
            .identity = {.sha256 = value.output.sha256, .bytes = value.output.bytes},
        });
        for (const auto& asset : value.assets) {
            std::visit(
                [&](const auto& item) {
                    using Value = std::remove_cvref_t<decltype(item)>;
                    const auto name = std::is_same_v<Value, ProtectionAsset> ? mask_name : psf_name;
                    result.push_back(
                        {.name = name, .identity = {.sha256 = item.sha256, .bytes = item.bytes}});
                },
                asset);
        }
        return result;
    } catch (const std::bad_alloc&) {
        return core::failure(core::ErrorCode::resource,
                             "Building the bundle inventory exhausted memory");
    }
}
core::Result<void> agrees(const RunRecord& declared, std::span<const core::NamedContent> present,
                          std::span<const std::string> directories) {
    auto expected = inventory(declared);
    if (!expected) {
        return std::unexpected(std::move(expected.error()));
    }
    if (!valid_present(present) || present.size() != expected->size() + 1) {
        return refusal("disagrees with the closed file inventory");
    }
    for (const auto& artifact : *expected) {
        const auto found = std::ranges::find(present, artifact.name, &core::NamedContent::name);
        if (found == present.end()) {
            return refusal("declares " + artifact.name + ", which is not here");
        }
        if (found->identity != artifact.identity) {
            return refusal("holds a " + artifact.name + " that is not the one recorded");
        }
    }
    // All canonical artifact paths have at most one parent, assets/. No empty/foreign directory.
    if (directories.size() > 1) {
        return refusal("holds undeclared directories");
    }
    for (const auto& directory : directories) {
        if (!core::valid_bundle_path(directory) || !implied_directory(*expected, directory)) {
            return refusal("holds a " + directory + " directory that no declared file needs");
        }
    }
    return {};
}
core::Result<RunRecord> read_record(std::span<const std::byte> bytes) {
    // char borrows the same immutable encoded bytes. The bounded parser never outlives this span.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const std::string_view text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    return Codec::parse<RunRecord>(
        text, {.bytes = record_max_bytes, .depth = record_max_depth, .events = record_max_events});
}
} // namespace docenhance::record
