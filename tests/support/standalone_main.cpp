// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
// Independent first-party reference test runner; not an alternative application build.
#include "architecture_cases.hpp"
#include "pipeline_cases.hpp"
#include "reference_cases.hpp"

#include <exception>
#include <iostream>
// NOLINTNEXTLINE(bugprone-exception-escape): test failures are caught and rendered below.
int main() {
    try {
        docenhance::tests::parser_cases();
        docenhance::tests::decimal_cases();
        docenhance::tests::numerical_cases();
        docenhance::tests::capabilities_cases();
        docenhance::tests::memory_cases();
        docenhance::tests::architecture_cases();
        docenhance::tests::schedule_cases();
        docenhance::tests::box_mean_cases();
        std::cout << "PASS: parser, numerical and capability reference suites\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
