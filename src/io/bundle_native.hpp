// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"
#include "png_context.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace docenhance::io {
// Retained native directory identity. On Windows denying delete sharing pins the pathname;
// POSIX accesses children relative to the retained descriptor, even if that name is moved.
class BundleDirectory {
  public:
    BundleDirectory() = default;
    BundleDirectory(const BundleDirectory&) = delete;
    BundleDirectory& operator=(const BundleDirectory&) = delete;
    BundleDirectory(BundleDirectory&& other) noexcept;
    BundleDirectory& operator=(BundleDirectory&& other) noexcept;
    ~BundleDirectory();
    [[nodiscard]] static core::Result<BundleDirectory> open(const std::filesystem::path& path);
    [[nodiscard]] core::Result<BundleDirectory> child(const std::string& name) const;
    [[nodiscard]] core::Result<FileHandle> file(const std::string& name) const;
    [[nodiscard]] core::Result<std::vector<std::string>> entries() const;

  private:
    void close() noexcept;
    std::intptr_t handle_ = -1;
    std::filesystem::path path_;
};
} // namespace docenhance::io
