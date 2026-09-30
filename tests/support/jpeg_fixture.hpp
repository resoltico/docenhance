// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "file_contents.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace docenhance::tests {
inline std::vector<std::uint8_t> jpeg_fixture(const std::string& name) {
    const auto path =
        std::filesystem::path{__FILE__}.parent_path().parent_path() / "fixtures" / "jpeg" / name;
    const auto bytes = file_contents(path);
    std::vector<std::uint8_t> result;
    result.reserve(bytes.size());
    for (const auto byte : bytes) {
        result.push_back(static_cast<std::uint8_t>(byte));
    }
    return result;
}
} // namespace docenhance::tests
