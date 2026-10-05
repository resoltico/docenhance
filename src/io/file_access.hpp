// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace docenhance::io {
struct FileCloser {
    void operator()(std::FILE* file) const noexcept;
};
using FileHandle = std::unique_ptr<std::FILE, FileCloser>;
// Opens a file for reading without building a codec context, for callers that only need bytes.
[[nodiscard]] FileHandle open_for_reading(const std::filesystem::path& path);
// Creates a file, never replacing one: the transaction owns an empty staging directory.
[[nodiscard]] FileHandle open_for_writing(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path utf8_path(std::string_view value);
// The UTF-8 bytes of a path. Where a platform spells paths in bytes those bytes are returned
// unchanged, so a name is recorded exactly as it was admitted; where it spells them in wide
// characters they are converted, never through the active code page, which cannot express the
// characters a document's name is most likely to carry.
[[nodiscard]] std::string utf8_spelling(const std::filesystem::path& value);
} // namespace docenhance::io
