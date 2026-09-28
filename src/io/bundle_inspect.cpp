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
#include <optional>
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
// The length of an open file, taken from the stream itself and leaving it positioned to read from
// the beginning. A file whose end is past what a stream position can express is reported as no
// length at all, because this reads only files that are bounded anyway.
[[nodiscard]] std::optional<std::size_t> file_position_at_end(std::FILE* file) {
    if (std::fseek(file, 0, SEEK_END) != 0) {
        return std::nullopt;
    }
    const auto end = std::ftell(file);
    if (end < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(end);
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
        const BundleSlot slot{.path = walk->path()};
        // Size and digest both come from this one reading, so no entry is recorded at a size that
        // a separate measurement claimed.
        auto identity = identify_slot(slot, bundle_max_file_bytes);
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
    const auto file = open_for_reading(path);
    if (file == nullptr) {
        return std::unexpected(refused("cannot open " + std::string(relative)));
    }
    // Measured through the handle this reads from, so the size belongs to the file being read
    // rather than to whatever the path named a moment earlier. A file past the bound reports no
    // position this can use, which is the same refusal as one that is simply too large.
    const auto measured = file_position_at_end(file.get());
    if (!measured || *measured > limit) {
        return std::unexpected(refused("holds a " + std::string(relative) + " that is too large"));
    }
    auto bytes = budget.allocate(*measured);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    const auto remaining = bytes->bytes();
    if (!remaining.empty() &&
        std::fread(remaining.data(), 1, remaining.size(), file.get()) != remaining.size()) {
        return std::unexpected(refused("cannot read " + std::string(relative)));
    }
    return bytes;
}
} // namespace docenhance::io
