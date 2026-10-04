// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "entry_identity.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <utility>
#ifdef _WIN32
#include "windows_sdk.hpp" // NOLINT(misc-include-cleaner): Native SDK prerequisite types precede direct API headers.

#include <array>
#include <bit>
#include <corecrt_io.h>
#include <fileapi.h>
#include <handleapi.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <stdio.h> // NOLINT(modernize-deprecated-headers): Native CRT _fdopen/_fileno declarations require stdio.h.
#include <winbase.h>
#include <winnt.h>
#else
#ifdef __APPLE__
#include <sys/fcntl.h>
#else
#include <fcntl.h>
#endif
#include <stdio.h> // NOLINT(modernize-deprecated-headers): POSIX fileno is not an ISO C++ cstdio declaration.
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace docenhance::io {
#ifdef _WIN32
std::optional<EntryIdentity> handle_identity(void* const handle) noexcept {
    BY_HANDLE_FILE_INFORMATION info{};
    FILE_ID_INFO identifier{};
    if (GetFileInformationByHandle(handle, &info) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        GetFileType(handle) != FILE_TYPE_DISK ||
        GetFileInformationByHandleEx(handle, FileIdInfo, &identifier,
                                     static_cast<DWORD>(sizeof(identifier))) == 0) {
        return std::nullopt;
    }
    const auto words = std::bit_cast<std::array<std::uint64_t, 2>>(identifier.FileId.Identifier);
    return EntryIdentity{
        .volume = identifier.VolumeSerialNumber,
        .object = words.front(),
        .object_high = words.back(),
    };
}
namespace {
HANDLE metadata_handle(const std::filesystem::path& path, bool directory) {
    auto* const handle = CreateFileW(
        path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return handle;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(handle, &info) == 0 ||
        ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory) {
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}
HANDLE object_handle(void* const named, const EntryIdentity& expected) {
    // The scalar descriptor represents an exactly zero-extended identifier, not a truncated one.
    // Every opened object is still compared using the complete 128-bit identity and volume.
    const auto size = static_cast<DWORD>(sizeof(FILE_ID_DESCRIPTOR));
    auto descriptor =
        expected.object_high == 0
            ? FILE_ID_DESCRIPTOR{.dwSize = size,
                                 .Type = FileIdType,
                                 .FileId = std::bit_cast<LARGE_INTEGER>(expected.object),}
            : FILE_ID_DESCRIPTOR{.dwSize = size,
                                 .Type = ExtendedFileIdType,
                                 .ExtendedFileId = std::bit_cast<FILE_ID_128>(
                                     std::array{expected.object, expected.object_high}),};
    auto* const handle =
        OpenFileById(named, &descriptor, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     nullptr, FILE_FLAG_BACKUP_SEMANTICS);
    if (handle != INVALID_HANDLE_VALUE && handle_identity(handle) != expected) {
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}
} // namespace
std::optional<EntryIdentity> stream_identity(std::FILE* file) noexcept {
    const auto native = _get_osfhandle(_fileno(file));
    if (native == -1) {
        return std::nullopt;
    }
    // CRT intptr_t handles are opaque OS tokens, never dereferenced as memory.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
    return handle_identity(reinterpret_cast<HANDLE>(native));
}
std::optional<EntryIdentity> entry_identity(const std::filesystem::path& path) {
    constexpr DWORD read = FILE_SHARE_READ;
    constexpr DWORD write = FILE_SHARE_WRITE;
    constexpr DWORD remove = FILE_SHARE_DELETE;
    constexpr DWORD reparse = FILE_FLAG_OPEN_REPARSE_POINT;
    constexpr DWORD backup = FILE_FLAG_BACKUP_SEMANTICS;
    auto* const handle = CreateFileW(path.c_str(), 0, read | write | remove, nullptr, OPEN_EXISTING,
                                     reparse | backup, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    const auto result = handle_identity(handle);
    CloseHandle(handle);
    return result;
}
EntryLease EntryLease::directory(const std::filesystem::path& path) {
    EntryLease result;
    auto* const handle = metadata_handle(path, true);
    if (handle != INVALID_HANDLE_VALUE) {
        const auto identity = handle_identity(handle);
        if (identity) {
            auto* const retained = object_handle(handle, *identity);
            if (retained != INVALID_HANDLE_VALUE) {
                result.handle_ = retained;
                result.identity_ = *identity;
            }
        }
        CloseHandle(handle);
    }
    return result;
}
EntryLease EntryLease::capture(std::FILE* file, const std::filesystem::path& path) {
    EntryLease result;
    auto* const handle = metadata_handle(path, false);
    if (handle != INVALID_HANDLE_VALUE) {
        const auto identity = handle_identity(handle);
        if (identity && identity == stream_identity(file)) {
            auto* const retained = object_handle(handle, *identity);
            if (retained != INVALID_HANDLE_VALUE) {
                result.handle_ = retained;
                result.identity_ = *identity;
            }
        }
        CloseHandle(handle);
    }
    return result;
}
void EntryLease::close() noexcept {
    if (handle_ != nullptr) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
}
EntryLease::EntryLease(EntryLease&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)), identity_(other.identity_) {}
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
    // fileno is the POSIX declaration exposed by stdio.h through SDK internals.
    // NOLINTNEXTLINE(misc-include-cleaner)
    return descriptor_identity(fileno(file));
}
std::optional<EntryIdentity> descriptor_identity(int handle) noexcept {
    struct stat status{};
    return fstat(handle, &status) == 0 ? native_identity(status) : std::nullopt;
}
std::optional<EntryIdentity> entry_identity(const std::filesystem::path& path) {
    struct stat status{};
    return lstat(path.c_str(), &status) == 0 ? native_identity(status) : std::nullopt;
}
EntryLease EntryLease::directory(const std::filesystem::path& path) {
    EntryLease result;
    constexpr int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const auto handle = ::open(path.c_str(), flags);
    if (handle >= 0) {
        struct stat status{};
        const auto identity = fstat(handle, &status) == 0 ? native_identity(status) : std::nullopt;
        if (identity && S_ISDIR(status.st_mode)) {
            result.handle_ = handle;
            result.identity_ = *identity;
        } else {
            static_cast<void>(::close(handle));
        }
    }
    return result;
}
EntryLease EntryLease::capture(std::FILE* file, const std::filesystem::path& /*path*/) {
    EntryLease result;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,misc-include-cleaner)
    const auto handle = fcntl(fileno(file), F_DUPFD_CLOEXEC, 0);
    if (handle >= 0) {
        struct stat status{};
        const auto identity = fstat(handle, &status) == 0 ? native_identity(status) : std::nullopt;
        if (identity && S_ISREG(status.st_mode)) {
            result.handle_ = handle;
            result.identity_ = *identity;
        } else {
            static_cast<void>(::close(handle));
        }
    }
    return result;
}
void EntryLease::close() noexcept {
    if (handle_ != -1) {
        static_cast<void>(::close(handle_));
        handle_ = -1;
    }
}
EntryLease::EntryLease(EntryLease&& other) noexcept
    : handle_(std::exchange(other.handle_, -1)), identity_(other.identity_) {}
#endif
EntryLease& EntryLease::operator=(EntryLease&& other) noexcept {
    if (this != &other) {
        close();
        std::swap(handle_, other.handle_);
        identity_ = other.identity_;
    }
    return *this;
}
EntryLease::~EntryLease() {
    close();
}
std::optional<EntryIdentity> EntryLease::identity() const noexcept {
#ifdef _WIN32
    const bool active = handle_ != nullptr;
#else
    const bool active = handle_ != -1;
#endif
    return active ? std::optional{identity_} : std::nullopt;
}
bool EntryLease::matches(const std::filesystem::path& path) const {
    const auto owned = identity();
    return owned && entry_identity(path) == owned;
}
} // namespace docenhance::io
