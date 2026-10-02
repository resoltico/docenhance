// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "png_context.hpp"

#include <filesystem>
#ifdef _WIN32
#include "windows_sdk.hpp" // NOLINT(misc-include-cleaner): SDK prerequisites precede direct API headers.

#include <corecrt_io.h>
#include <cstdint>
#include <fcntl.h>
#include <fileapi.h>
#include <handleapi.h>
#include <minwindef.h>
#include <stdio.h> // NOLINT(modernize-deprecated-headers): Native CRT stream/descriptor declarations.
#include <winbase.h>
#include <winnt.h>
#else
#ifdef __APPLE__
#include <sys/fcntl.h>
#else
#include <fcntl.h>
#endif
#include <stdio.h> // NOLINT(modernize-deprecated-headers): POSIX fdopen declaration.
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace docenhance::io {
FileHandle open_for_reading(const std::filesystem::path& path) {
#ifdef _WIN32
    auto* const handle = CreateFileW(path.c_str(), GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {};
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(handle) != FILE_TYPE_DISK || GetFileInformationByHandle(handle, &info) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        CloseHandle(handle);
        return {};
    }
    // The CRT takes ownership of the validated, already opened regular file.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto native = reinterpret_cast<std::intptr_t>(handle);
    const auto descriptor = _open_osfhandle(native, _O_RDONLY | _O_BINARY);
    if (descriptor < 0) {
        CloseHandle(handle);
        return {};
    }
    auto* const stream = _fdopen(descriptor, "rb");
    if (stream == nullptr) {
        _close(descriptor);
    }
#else
    // Following a source link resolves it once; nonblocking open prevents a replaced FIFO from
    // blocking before regular-file admission. Every subsequent read uses this descriptor.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const auto descriptor = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0) {
        return {};
    }
    struct stat status{};
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode)) {
        static_cast<void>(::close(descriptor));
        return {};
    }
    // NOLINTNEXTLINE(misc-include-cleaner): fdopen is provided through the POSIX stdio SDK wrapper.
    auto* const stream = fdopen(descriptor, "rb");
    if (stream == nullptr) {
        static_cast<void>(::close(descriptor));
    }
#endif
    return FileHandle{stream};
}
FileHandle open_for_writing(const std::filesystem::path& path) {
#ifdef _WIN32
    auto* const handle = CreateFileW(path.c_str(), GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto native = reinterpret_cast<std::intptr_t>(handle);
    const auto descriptor = _open_osfhandle(native, _O_WRONLY | _O_BINARY | _O_NOINHERIT);
    if (descriptor < 0) {
        CloseHandle(handle);
        return {};
    }
    auto* const stream = _fdopen(descriptor, "wb");
    if (stream == nullptr) {
        _close(descriptor);
    }
#else
    constexpr unsigned int owner_read_write = 0600;
    constexpr int flags = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const auto descriptor = ::open(path.c_str(), flags, owner_read_write);
    if (descriptor < 0) {
        return {};
    }
    // NOLINTNEXTLINE(misc-include-cleaner): fdopen is provided through the POSIX stdio SDK wrapper.
    auto* const stream = fdopen(descriptor, "wb");
    if (stream == nullptr) {
        static_cast<void>(::close(descriptor));
    }
#endif
    return FileHandle{stream};
}
} // namespace docenhance::io
