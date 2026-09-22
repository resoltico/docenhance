// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "publication.hpp"

#include <cerrno>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elifdef __linux__
#include <fcntl.h>
#include <stdio.h> // NOLINT(modernize-deprecated-headers)
#elifdef __APPLE__
#include <stdio.h> // NOLINT(modernize-deprecated-headers)
#else
#error "Exclusive publication requires a supported native no-replace rename"
#endif

namespace docenhance::io {
std::error_code rename_exclusive(const std::filesystem::path& source,
                                 const std::filesystem::path& target) noexcept {
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), target.c_str(), 0) != 0) {
        return {};
    }
    return {static_cast<int>(GetLastError()), std::system_category()};
#elifdef __linux__
    if (renameat2(AT_FDCWD, source.c_str(), AT_FDCWD, target.c_str(), RENAME_NOREPLACE) == 0) {
        return {};
    }
    return {errno, std::generic_category()};
#else
    if (renamex_np(source.c_str(), target.c_str(), RENAME_EXCL) == 0) {
        return {};
    }
    return {errno, std::generic_category()};
#endif
}
bool definitely_not_published(const std::error_code& error) noexcept {
    // An I/O/network error can be ambiguous (e.g. a remote rename committed before a lost reply).
    // Only errors that prove refusal are reported as not_published.
    return error == std::errc::file_exists || error == std::errc::directory_not_empty ||
           error == std::errc::permission_denied || error == std::errc::read_only_file_system ||
           error == std::errc::cross_device_link || error == std::errc::function_not_supported ||
           error == std::errc::operation_not_supported || error == std::errc::invalid_argument;
}
} // namespace docenhance::io
