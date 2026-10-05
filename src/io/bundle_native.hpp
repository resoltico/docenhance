// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "entry_identity.hpp"
#include "file_access.hpp"

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
    [[nodiscard]] bool bound() const;

  private:
    [[nodiscard]] bool active() const noexcept {
#ifdef _WIN32
        return handle_ != nullptr;
#else
        return handle_ != -1;
#endif
    }
    // Native one-component access, not admission of a portable artifact path.
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
#else
    int handle_ = -1;
#endif
    std::filesystem::path path_;
};
} // namespace docenhance::io
