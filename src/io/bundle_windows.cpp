// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include "bundle_native.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/bundle.hpp"
#include "png_context.hpp"

#include <cstdint>
#include <cstdio>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <io.h>
#include <string>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
namespace docenhance::io {
namespace {
core::Error refused() {
    return {.code = core::ErrorCode::input,
            .message = "Cannot access a regular, non-reparse bundle entry"};
}
HANDLE open_entry(const std::filesystem::path& path, bool directory) {
    constexpr DWORD reparse = FILE_FLAG_OPEN_REPARSE_POINT;
    constexpr DWORD backup = FILE_FLAG_BACKUP_SEMANTICS;
    const DWORD flags = reparse | backup;
    // NOLINTNEXTLINE(misc-include-cleaner)
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, flags, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return handle;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    // NOLINTNEXTLINE(misc-include-cleaner)
    const bool valid = GetFileInformationByHandle(handle, &info) != 0 &&
                       (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                       ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory &&
                       // NOLINTNEXTLINE(misc-include-cleaner)
                       GetFileType(handle) == FILE_TYPE_DISK;
    if (!valid) {
        // NOLINTNEXTLINE(misc-include-cleaner)
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}
} // namespace
BundleDirectory::BundleDirectory(BundleDirectory&& other) noexcept
    : handle_(std::exchange(other.handle_, -1)), path_(std::move(other.path_)) {}
BundleDirectory& BundleDirectory::operator=(BundleDirectory&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, -1);
        path_ = std::move(other.path_);
    }
    return *this;
}
void BundleDirectory::close() noexcept {
    if (handle_ != -1) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,misc-include-cleaner)
        CloseHandle(reinterpret_cast<HANDLE>(handle_));
        handle_ = -1;
    }
}
BundleDirectory::~BundleDirectory() {
    close();
}
core::Result<BundleDirectory> BundleDirectory::open(const std::filesystem::path& path) {
    BundleDirectory result;
    result.path_ = std::filesystem::absolute(path);
    const auto handle = open_entry(result.path_, true);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(refused());
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    result.handle_ = reinterpret_cast<std::intptr_t>(handle);
    return result;
}
core::Result<BundleDirectory> BundleDirectory::child(const std::string& name) const {
    return open(path_ / utf8_path(name));
}
core::Result<FileHandle> BundleDirectory::file(const std::string& name) const {
    const auto handle = open_entry(path_ / utf8_path(name), false);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(refused());
    }
    const auto descriptor =
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        _open_osfhandle(
            reinterpret_cast<std::intptr_t>(handle),
            static_cast<int>(static_cast<unsigned>(_O_RDONLY) | static_cast<unsigned>(_O_BINARY)));
    if (descriptor < 0) {
        // NOLINTNEXTLINE(misc-include-cleaner)
        CloseHandle(handle);
        return std::unexpected(refused());
    }
    auto* const stream = _fdopen(descriptor, "rb");
    if (stream == nullptr) {
        _close(descriptor);
        return std::unexpected(refused());
    }
    return FileHandle{stream};
}
core::Result<std::vector<std::string>> BundleDirectory::entries() const {
    WIN32_FIND_DATAW data{};
    // NOLINTNEXTLINE(misc-include-cleaner)
    const auto search = FindFirstFileW((path_ / L"*").c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) {
        return std::unexpected(refused());
    }
    std::vector<std::string> names;
    bool failed = false;
    try {
        do {
            const auto name = utf8_spelling(std::filesystem::path{data.cFileName});
            if (name == "." || name == "..") {
                continue;
            }
            if (names.size() == bundle_max_entries) {
                failed = true;
                break;
            }
            names.push_back(name);
            // NOLINTNEXTLINE(misc-include-cleaner)
        } while (FindNextFileW(search, &data) != 0);
        // NOLINTNEXTLINE(misc-include-cleaner)
        failed = failed || GetLastError() != ERROR_NO_MORE_FILES;
    } catch (...) {
        // NOLINTNEXTLINE(misc-include-cleaner)
        FindClose(search);
        return core::failure(core::ErrorCode::resource, "Bundle enumeration exhausted memory");
    }
    // NOLINTNEXTLINE(misc-include-cleaner)
    failed = FindClose(search) == 0 || failed;
    if (failed) {
        return std::unexpected(refused());
    }
    return names;
}
} // namespace docenhance::io
#endif
