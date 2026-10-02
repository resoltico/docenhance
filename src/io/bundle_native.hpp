// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/result.hpp"
#include "png_context.hpp"

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
    [[nodiscard]] bool active() const noexcept {
#ifdef _WIN32
        return handle_ != nullptr;
#else
        return handle_ != -1;
#endif
    }
    [[nodiscard]] static bool valid_name(const std::string& name) noexcept {
        const bool relative = !name.empty() && name != "." && name != ".." &&
                              !name.contains('\0') && !name.contains('/');
#ifdef _WIN32
        return relative && !name.contains('\\') && !name.contains(':');
#else
        return relative;
#endif
    }

    void close() noexcept;
#ifdef _WIN32
    void* handle_ = nullptr;
    std::filesystem::path path_;
#else
    int handle_ = -1;
#endif
};
} // namespace docenhance::io
