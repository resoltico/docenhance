// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace docenhance::tests {
// A newly created, uniquely named directory below the system temporary directory; the test owns
// it and everything inside it is removed afterwards.
class TemporaryDirectory {
  public:
    explicit TemporaryDirectory(std::string_view prefix)
        : path(std::filesystem::temp_directory_path() /
               (std::string{prefix} + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        REQUIRE(std::filesystem::create_directory(path));
    }
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
