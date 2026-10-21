// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once

namespace docenhance::tests {
[[noreturn]] inline void throw_nonstandard_execution_failure() {
    throw 42; // NOLINT(bugprone-std-exception-baseclass): Exercise catch-all containment.
}
} // namespace docenhance::tests
