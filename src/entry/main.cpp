// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/cli/run.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/host/processor.hpp"

#include <cstddef>
#include <iostream>
#include <span>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <string>
#include <utility>
#include <windows.h> // NOLINT(misc-include-cleaner)

namespace docenhance::entry {
namespace {
core::Result<std::string> utf8_argument(const wchar_t* const value) {
    constexpr unsigned int code_page = CP_UTF8;                      // NOLINT(misc-include-cleaner)
    constexpr unsigned long conversion_flags = WC_ERR_INVALID_CHARS; // NOLINT(misc-include-cleaner)
    const auto convert = &WideCharToMultiByte;                       // NOLINT(misc-include-cleaner)
    const int count = convert(code_page, conversion_flags, value, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return core::failure(core::ErrorCode::argument, "An argument is not valid UTF-16");
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    if (convert(code_page, conversion_flags, value, -1, result.data(), count, nullptr, nullptr) !=
        count) {
        return core::failure(core::ErrorCode::argument, "Cannot convert an argument to UTF-8");
    }
    result.pop_back();
    return result;
}
int run_windows(std::span<wchar_t* const> raw) {
    std::vector<std::string> owned;
    owned.reserve(raw.size());
    for (const wchar_t* const value : raw) {
        auto converted = utf8_argument(value);
        if (!converted) {
            return static_cast<int>(converted.error().exit_code());
        }
        owned.push_back(std::move(*converted));
    }
    std::vector<const char*> args;
    args.reserve(owned.size());
    for (const auto& value : owned) {
        args.push_back(value.c_str());
    }
    host::Processor processor;
    return cli::run(args, processor, std::cout, std::cerr);
}
} // namespace
} // namespace docenhance::entry

// NOLINTNEXTLINE(misc-const-correctness,misc-use-internal-linkage)
int wmain(int argc, wchar_t** const argv) {
    try {
        return docenhance::entry::run_windows({argv, static_cast<std::size_t>(argc)});
    } catch (...) {
        return static_cast<int>(docenhance::core::ExitCode::invariant);
    }
}
#else
int main(int argc, char** const argv) { // NOLINT(misc-const-correctness)
    try {
        const std::span<char* const> raw(argv, static_cast<std::size_t>(argc));
        const std::vector<const char*> args(raw.begin(), raw.end());
        docenhance::host::Processor processor;
        return docenhance::cli::run(args, processor, std::cout, std::cerr);
    } catch (...) {
        // Reporting the original failure failed too (for example, out of memory).
        return static_cast<int>(docenhance::core::ExitCode::invariant);
    }
}

#endif
