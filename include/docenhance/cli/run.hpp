// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"

#include <ostream>
#include <span>
namespace docenhance::cli {
// args[0] is the program name; argument pointers refer to caller-owned NUL-terminated UTF-8.
// Admit, dispatch and render once, then write/flush only the stream carrying response content.
// Delivery is unformatted; existing stream ties and exception masks remain in effect.
// Rendering, write or flush failure returns exit 5 without a second response or reexecution.
// The JSON exit_code describes the rendered outcome; a later delivery failure can override the
// process status. Exit 5 or a missing response is not evidence that publication did not commit.
[[nodiscard]] int run(std::span<const char* const> args, app::Processor& processor,
                      std::ostream& out, std::ostream& err,
                      const core::Cancellation& cancellation = {});
} // namespace docenhance::cli
