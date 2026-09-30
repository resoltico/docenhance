// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
namespace docenhance::tests {
// Assert preserved bytes, rather than only an equally sized replacement.
inline std::string file_contents(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
} // namespace docenhance::tests
