// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/io/paths.hpp"
#include "file_access.hpp"

#include <concepts>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#ifdef _WIN32
#include "windows_sdk.hpp" // NOLINT(misc-include-cleaner): SDK prerequisites precede direct API headers.

#include <corecrt_io.h>
#include <cstdint>
#include <fcntl.h>
#include <fileapi.h>
#include <handleapi.h>
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
namespace {
// A template so that only the branch for this platform's path type is ever instantiated: in a
// plain function the other branch would still have to type-check, and returning a wide string as
// a narrow one does not.
template <typename Path> std::string spelled_in_utf8(const Path& value) {
    if constexpr (std::same_as<typename Path::value_type, char>) {
        return value.native();
    } else {
        std::string bytes;
        for (const char8_t unit : value.u8string()) {
            bytes.push_back(static_cast<char>(unit));
        }
        return bytes;
    }
}
} // namespace
void FileCloser::operator()(std::FILE* file) const noexcept {
    // Best-effort cleanup. Writers check flush/close explicitly before reporting success.
    static_cast<void>(std::fclose(file)); // NOLINT(cppcoreguidelines-owning-memory)
}

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
std::string utf8_spelling(const std::filesystem::path& value) {
    return spelled_in_utf8(value);
}
std::filesystem::path utf8_path(std::string_view value) {
    // The admitted spelling is already UTF-8 bytes. Where a path stores char those bytes are its
    // native representation, so they are kept verbatim; only a wchar_t path (Windows) needs the
    // char8_t overload, which decodes them instead of applying the active code page. Neither
    // branch normalizes or repairs the identity bytes.
    if constexpr (std::same_as<std::filesystem::path::value_type, char>) {
        return std::filesystem::path{std::string{value}};
    } else {
        std::u8string utf8;
        utf8.reserve(value.size());
        for (const char byte : value) {
            utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
        }
        return std::filesystem::path{utf8};
    }
}
std::string file_name(const std::string& path) {
    const auto name = utf8_spelling(utf8_path(path).filename());
    return name.empty() ? path : name;
}
} // namespace docenhance::io
