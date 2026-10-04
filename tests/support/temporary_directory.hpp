// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <exception>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

namespace docenhance::tests {
// A newly created, uniquely named directory below the system temporary directory; the test owns
// it and everything inside it is removed afterwards. Concurrent test processes may share a prefix,
// so the name is not trusted to be unique: creating a directory is atomic and reports whether this
// call made it, and a name that already exists is simply tried again with a different suffix.
class TemporaryDirectory {
  public:
    explicit TemporaryDirectory(std::string_view prefix) : path(create(prefix)) {}
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;
    ~TemporaryDirectory() {
        try {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        } catch (...) {
            // A failed test cleanup must not silently pass the test process.
            std::terminate();
        }
    }
    std::filesystem::path path;

  private:
    static std::filesystem::path create(std::string_view prefix) {
        std::random_device entropy;
        constexpr int attempts = 64;
        for (int attempt = 0; attempt < attempts; ++attempt) {
            auto candidate =
                std::filesystem::temp_directory_path() /
                (std::string{prefix} + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(entropy()));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                return candidate;
            }
        }
        FAIL("No unique temporary directory could be created for " << prefix);
        return {};
    }
};
// Compares iterators rather than calling std::filesystem::is_empty, whose MSVC implementation
// combines internal stat flags that clang-analyzer reports as out-of-range enum values.
[[nodiscard]] inline bool empty_directory(const std::filesystem::path& path) {
    return std::filesystem::directory_iterator(path) == std::filesystem::directory_iterator{};
}
// The UTF-8 spelling that command admission receives for this native path.
[[nodiscard]] inline std::string utf8_spelling(const std::filesystem::path& path) {
    std::string result;
    for (const char8_t byte : path.u8string()) {
        result.push_back(static_cast<char>(byte));
    }
    return result;
}
} // namespace docenhance::tests
