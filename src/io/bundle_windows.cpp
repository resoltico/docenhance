// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#ifdef _WIN32
#include "bundle_native.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/bundle.hpp"
#include "png_context.hpp"
#include "windows_sdk.hpp"

#include <corecrt_io.h>
#include <corecrt_stdio.h>
#include <cstdint>
#include <errhandlingapi.h>
#include <expected>
#include <fcntl.h>
#include <fileapi.h>
#include <filesystem>
#include <handleapi.h>
#include <iterator>
#include <minwindef.h>
#include <string>
#include <utility>
#include <vector>
#include <winbase.h>
#include <winnt.h>
namespace docenhance::io {
namespace {
core::Error refused() {
    return {
        .code = core::ErrorCode::input,
        .message = "Cannot access a regular, non-reparse bundle entry",
    };
}
HANDLE open_entry(const std::filesystem::path& path, bool directory) {
    constexpr DWORD reparse = FILE_FLAG_OPEN_REPARSE_POINT;
    constexpr DWORD backup = FILE_FLAG_BACKUP_SEMANTICS;
    const DWORD flags = reparse | backup;
    auto* const handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                     OPEN_EXISTING, flags, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return handle;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid = GetFileInformationByHandle(handle, &info) != 0 &&
                       (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0 &&
                       ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory &&
                       GetFileType(handle) == FILE_TYPE_DISK;
    if (!valid) {
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}
} // namespace
BundleDirectory::BundleDirectory(BundleDirectory&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)), path_(std::move(other.path_)) {}
BundleDirectory& BundleDirectory::operator=(BundleDirectory&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
        path_ = std::move(other.path_);
    }
    return *this;
}
void BundleDirectory::close() noexcept {
    if (handle_ != nullptr) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}
BundleDirectory::~BundleDirectory() {
    close();
}
core::Result<BundleDirectory> BundleDirectory::open(const std::filesystem::path& path) {
    BundleDirectory result;
    result.path_ = std::filesystem::absolute(path);
    auto* const handle = open_entry(result.path_, true);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(refused());
    }
    result.handle_ = handle;
    return result;
}
core::Result<BundleDirectory> BundleDirectory::child(const std::string& name) const {
    return open(path_ / utf8_path(name));
}
core::Result<FileHandle> BundleDirectory::file(const std::string& name) const {
    auto* const handle = open_entry(path_ / utf8_path(name), false);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(refused());
    }
    // CRT descriptor ownership requires the native handle's intptr_t representation.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto native = reinterpret_cast<std::intptr_t>(handle);
    const auto flags =
        static_cast<int>(static_cast<unsigned>(_O_RDONLY) | static_cast<unsigned>(_O_BINARY));
    const auto descriptor = _open_osfhandle(native, flags);
    if (descriptor < 0) {
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
    auto* const search = FindFirstFileW((path_ / L"*").c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) {
        return std::unexpected(refused());
    }
    std::vector<std::string> names;
    bool failed = false;
    try {
        while (true) {
            const auto name = utf8_spelling(std::filesystem::path{std::begin(data.cFileName)});
            if (name != "." && name != "..") {
                if (names.size() == bundle_max_entries) {
                    failed = true;
                    break;
                }
                names.push_back(name);
            }
            if (FindNextFileW(search, &data) == 0) {
                break;
            }
        }
        failed = failed || GetLastError() != ERROR_NO_MORE_FILES;
    } catch (...) {
        FindClose(search);
        return core::failure(core::ErrorCode::resource, "Bundle enumeration exhausted memory");
    }
    failed = FindClose(search) == 0 || failed;
    if (failed) {
        return std::unexpected(refused());
    }
    return names;
}
} // namespace docenhance::io
#endif
