// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "entry_identity.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#ifdef _WIN32
#include "windows_sdk.hpp"

#include <corecrt_io.h>
#include <corecrt_stdio.h>
#include <fileapi.h>
#include <handleapi.h>
#include <minwindef.h>
#include <winbase.h>
#include <winnt.h>
#else
#include <stdio.h> // NOLINT(modernize-deprecated-headers): POSIX fileno is not an ISO C++ cstdio declaration.
#include <sys/stat.h>
#endif
namespace docenhance::io {
#ifdef _WIN32
namespace {
std::optional<EntryIdentity> native_identity(HANDLE handle) noexcept {
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(handle, &info) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return std::nullopt;
    }
    constexpr unsigned half = 32;
    return EntryIdentity{
        .volume = info.dwVolumeSerialNumber,
        .object = (std::uint64_t{info.nFileIndexHigh} << half) | info.nFileIndexLow,
    };
}
} // namespace
std::optional<EntryIdentity> stream_identity(std::FILE* file) noexcept {
    const auto native = _get_osfhandle(_fileno(file));
    if (native == -1) {
        return std::nullopt;
    }
    // CRT intptr_t handles are opaque OS tokens, never dereferenced as memory.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
    return native_identity(reinterpret_cast<HANDLE>(native));
}
std::optional<EntryIdentity> entry_identity(const std::filesystem::path& path) {
    constexpr DWORD read = FILE_SHARE_READ;
    constexpr DWORD write = FILE_SHARE_WRITE;
    constexpr DWORD remove = FILE_SHARE_DELETE;
    constexpr DWORD reparse = FILE_FLAG_OPEN_REPARSE_POINT;
    constexpr DWORD backup = FILE_FLAG_BACKUP_SEMANTICS;
    auto* const handle = CreateFileW(path.c_str(), GENERIC_READ, read | write | remove, nullptr,
                                     OPEN_EXISTING, reparse | backup, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    const auto result = native_identity(handle);
    CloseHandle(handle);
    return result;
}
#else
namespace {
std::optional<EntryIdentity> native_identity(const struct stat& status) noexcept {
    if (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode)) {
        return std::nullopt;
    }
    return EntryIdentity{
        .volume = static_cast<std::uint64_t>(status.st_dev),
        .object = static_cast<std::uint64_t>(status.st_ino),
    };
}
} // namespace
std::optional<EntryIdentity> stream_identity(std::FILE* file) noexcept {
    struct stat status{};
    // fileno is the POSIX declaration exposed by stdio.h through SDK internals.
    // NOLINTNEXTLINE(misc-include-cleaner)
    return fstat(fileno(file), &status) == 0 ? native_identity(status) : std::nullopt;
}
std::optional<EntryIdentity> entry_identity(const std::filesystem::path& path) {
    struct stat status{};
    return lstat(path.c_str(), &status) == 0 ? native_identity(status) : std::nullopt;
}
#endif
} // namespace docenhance::io
