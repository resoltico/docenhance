// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#ifndef _WIN32
#include "bundle_native.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/io/bundle.hpp"
#include "png_context.hpp"

#include <cerrno>
#include <dirent.h>
#include <expected>
#ifdef __APPLE__
#include <sys/fcntl.h>
#else
#include <fcntl.h>
#endif
#include <filesystem>
#include <iterator>
#include <stdio.h> // NOLINT(modernize-deprecated-headers): POSIX fdopen is declared by the public native stdio header.
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>
namespace docenhance::io {
namespace {
core::Error refused() {
    return {
        .code = core::ErrorCode::input,
        .message = "Cannot access a regular, non-link bundle entry",
    };
}
constexpr int directory_flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
} // namespace
BundleDirectory::BundleDirectory(BundleDirectory&& other) noexcept
    : handle_(std::exchange(other.handle_, -1)) {}
BundleDirectory& BundleDirectory::operator=(BundleDirectory&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, -1);
    }
    return *this;
}
void BundleDirectory::close() noexcept {
    if (handle_ != -1) {
        static_cast<void>(::close(handle_));
        handle_ = -1;
    }
}
BundleDirectory::~BundleDirectory() {
    close();
}
core::Result<BundleDirectory> BundleDirectory::open(const std::filesystem::path& path) {
    // Native open ABI; no creation mode is passed.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const auto descriptor = ::open(path.c_str(), directory_flags);
    if (descriptor < 0) {
        return std::unexpected(refused());
    }
    BundleDirectory result;
    result.handle_ = descriptor;
    return result;
}
core::Result<BundleDirectory> BundleDirectory::child(const std::string& name) const {
    if (!active() || !valid_name(name)) {
        return std::unexpected(refused());
    }
    // Native openat ABI; no creation mode is passed.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const auto descriptor = openat(handle_, name.c_str(), directory_flags);
    if (descriptor < 0) {
        return std::unexpected(refused());
    }
    BundleDirectory result;
    result.handle_ = descriptor;
    return result;
}
core::Result<FileHandle> BundleDirectory::file(const std::string& name) const {
    if (!active() || !valid_name(name)) {
        return std::unexpected(refused());
    }
    constexpr int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
    // Native openat ABI; no creation mode is passed.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const auto descriptor = openat(handle_, name.c_str(), flags);
    if (descriptor < 0) {
        return std::unexpected(refused());
    }
    struct stat status{};
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode)) {
        static_cast<void>(::close(descriptor));
        return std::unexpected(refused());
    }
    // The stream takes ownership of the already validated descriptor.
    // POSIX fdopen is supplied through the public stdio.h SDK wrapper.
    // NOLINTNEXTLINE(misc-include-cleaner)
    auto* const stream = fdopen(descriptor, "rb");
    if (stream == nullptr) {
        static_cast<void>(::close(descriptor));
        return std::unexpected(refused());
    }
    return FileHandle{stream};
}
core::Result<std::vector<std::string>> BundleDirectory::entries() const {
    if (!active()) {
        return std::unexpected(refused());
    }
    // A new descriptor gives this enumeration its own directory offset.
    // Native openat ABI; no creation mode is passed.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const auto descriptor = openat(handle_, ".", directory_flags);
    if (descriptor < 0) {
        return std::unexpected(refused());
    }
    auto* const directory = fdopendir(descriptor);
    if (directory == nullptr) {
        static_cast<void>(::close(descriptor));
        return std::unexpected(refused());
    }
    std::vector<std::string> names;
    bool failed = false;
    try {
        while (true) {
            errno = 0;
            // Private DIR storage is copied before another read; callers never share this DIR.
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            const auto* const entry = readdir(directory);
            if (entry == nullptr) {
                failed = errno != 0;
                break;
            }
            const std::string name{std::begin(entry->d_name)};
            if (name == "." || name == "..") {
                continue;
            }
            if (names.size() == bundle_max_entries) {
                failed = true;
                break;
            }
            names.push_back(name);
        }
    } catch (...) {
        static_cast<void>(closedir(directory));
        return core::failure(core::ErrorCode::resource, "Bundle enumeration exhausted memory");
    }
    failed = closedir(directory) != 0 || failed;
    if (failed) {
        return std::unexpected(refused());
    }
    return names;
}
} // namespace docenhance::io
#endif
