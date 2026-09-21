// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/cli/run.hpp"
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <iostream>
#include <span>
#include <vector>
int main(int argc, char** const argv) { // NOLINT(misc-const-correctness)
    try {
        const std::span<char* const> raw(argv, static_cast<std::size_t>(argc));
        const std::vector<const char*> args(raw.begin(), raw.end());
        return docenhance::cli::run(args, std::cout, std::cerr);
    } catch (...) {
        // Reporting the original failure failed too (for example, out of memory).
        return static_cast<int>(docenhance::core::ExitCode::invariant);
    }
}
