// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/process.hpp"

#include <ostream>
#include <span>
namespace docenhance::cli {
// Parses argv-style arguments (args[0] is the program name), dispatches, writes the response to
// out and err, and returns the process exit code. Contains parse/execution and transport failures
// separately.
[[nodiscard]] int run(std::span<const char* const> args, app::Processor& processor,
                      std::ostream& out, std::ostream& err);
} // namespace docenhance::cli
