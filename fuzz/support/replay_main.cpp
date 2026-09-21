// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
// Replays fuzz inputs through one harness without a fuzzing engine, so every build and compiler
// re-checks the seed corpus and every recorded regression. Arguments are files or directories
// (searched recursively, in sorted order). Replaying zero inputs is an error, not a pass.
#include "support/entry_point.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>
namespace {
void add_directory(const std::filesystem::path& directory,
                   std::vector<std::filesystem::path>& inputs) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            inputs.push_back(entry.path());
        }
    }
}
std::vector<std::filesystem::path> collect(std::span<char* const> arguments) {
    std::vector<std::filesystem::path> inputs;
    for (const std::string_view argument : arguments) {
        const std::filesystem::path path(argument);
        if (std::filesystem::is_directory(path)) {
            add_directory(path, inputs);
        } else if (std::filesystem::is_regular_file(path)) {
            inputs.push_back(path);
        } else {
            throw std::runtime_error("No such fuzz input: " + path.string());
        }
    }
    std::ranges::sort(inputs);
    return inputs;
}
std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        throw std::runtime_error("Cannot open fuzz input " + path.string());
    }
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream),
                                    std::istreambuf_iterator<char>()};
    if (stream.bad()) {
        throw std::runtime_error("Cannot read fuzz input " + path.string());
    }
    return bytes;
}
int replay(std::span<char* const> arguments) {
    const auto inputs = collect(arguments);
    for (const auto& path : inputs) {
        const auto bytes = read(path);
        static_cast<void>(LLVMFuzzerTestOneInput(bytes.data(), bytes.size()));
    }
    std::cout << "Replayed " << inputs.size() << " fuzz inputs\n";
    return inputs.empty() ? 1 : 0;
}
} // namespace
int main(int argc, char** const argv) { // NOLINT(misc-const-correctness)
    try {
        return replay(std::span<char* const>(argv, static_cast<std::size_t>(argc)).subspan(1));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    } catch (...) {
        std::cerr << "Unknown non-standard exception\n";
    }
    return 1;
}
