// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/bundle.hpp"
#include "docenhance/io/digest.hpp"
#include "png_context.hpp"
#include "publication.hpp"

#include <cstddef>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace docenhance::io {
namespace {
[[nodiscard]] core::Error refused(std::string detail) {
    return {.code = core::ErrorCode::input, .message = "The bundle " + std::move(detail)};
}

// The path a bundle names, relative to its root, with forward slashes on every platform.
[[nodiscard]] std::string relative_name(const std::filesystem::path& root,
                                        const std::filesystem::path& entry) {
    auto relative = entry.lexically_relative(root).generic_string();
    return relative;
}
} // namespace

core::Result<BundleContents> inspect_bundle(const std::string& directory) {
    const auto root = utf8_path(directory);
    std::error_code error;
    if (!std::filesystem::is_directory(root, error) || error) {
        return std::unexpected(refused("is not a readable directory"));
    }
    BundleContents contents;
    std::filesystem::recursive_directory_iterator walk{
        root, std::filesystem::directory_options::none, error};
    if (error) {
        return std::unexpected(refused("cannot be listed"));
    }
    const std::filesystem::recursive_directory_iterator end;
    for (std::size_t visited = 0; walk != end; walk.increment(error), ++visited) {
        if (error) {
            return std::unexpected(refused("cannot be listed"));
        }
        if (visited >= bundle_max_entries) {
            return std::unexpected(refused("holds more entries than one may contain"));
        }
        // Inspected without following: a link is refused, never resolved.
        const auto status = walk->symlink_status(error);
        if (error) {
            return std::unexpected(refused("holds an entry that cannot be inspected"));
        }
        auto name = relative_name(root, walk->path());
        if (std::filesystem::is_directory(status)) {
            contents.directories.push_back(std::move(name));
            continue;
        }
        if (!std::filesystem::is_regular_file(status)) {
            return std::unexpected(refused("holds " + name + ", which is not a regular file"));
        }
        const auto size = std::filesystem::file_size(walk->path(), error);
        if (error || size > bundle_max_file_bytes) {
            return std::unexpected(refused("holds " + name + ", which cannot be measured"));
        }
        const BundleSlot slot{.path = walk->path()};
        auto identity = identify_slot(slot);
        if (!identity) {
            return std::unexpected(refused("holds " + name + ", which cannot be read"));
        }
        contents.files.push_back({.name = std::move(name), .identity = std::move(*identity)});
    }
    return contents;
}

core::Result<core::Buffer> read_bundle_file(const std::string& directory, std::string_view relative,
                                            std::size_t limit, core::Budget& budget) {
    const auto path = utf8_path(directory) / utf8_path(relative);
    std::error_code error;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) || error) {
        return std::unexpected(refused("does not contain " + std::string(relative)));
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::unexpected(refused("cannot measure " + std::string(relative)));
    }
    if (size > limit) {
        return std::unexpected(refused("holds a " + std::string(relative) + " that is too large"));
    }
    auto bytes = budget.allocate(static_cast<std::size_t>(size));
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    const auto file = open_for_reading(path);
    if (file == nullptr) {
        return std::unexpected(refused("cannot open " + std::string(relative)));
    }
    const auto remaining = bytes->bytes();
    if (!remaining.empty() &&
        std::fread(remaining.data(), 1, remaining.size(), file.get()) != remaining.size()) {
        return std::unexpected(refused("cannot read " + std::string(relative)));
    }
    return bytes;
}
} // namespace docenhance::io
