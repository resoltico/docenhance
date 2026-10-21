// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
// Independent first-party reference test runner; not an alternative application build.
#include "architecture_cases.hpp"
#include "otsu_cases.hpp"
#include "pipeline_cases.hpp"
#include "reference_cases.hpp"
#include "require.hpp"
#include "sharpening_cases.hpp"

#include <atomic>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void run_reference(std::string_view name, void (&suite)()) {
    const auto before = docenhance::tests::assertion_counter().load(std::memory_order_relaxed);
    suite();
    const auto count =
        docenhance::tests::assertion_counter().load(std::memory_order_relaxed) - before;
    if (count == 0) {
        throw std::runtime_error("reference suite executed no assertions: " + std::string(name));
    }
    std::cout << "REFERENCE " << name << ' ' << count << '\n';
}
} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape): test failures are caught and rendered below.
int main() {
    try {
        run_reference("parser_cases", docenhance::tests::parser_cases);
        run_reference("decimal_cases", docenhance::tests::decimal_cases);
        run_reference("numerical_cases", docenhance::tests::numerical_cases);
        run_reference("capabilities_cases", docenhance::tests::capabilities_cases);
        run_reference("memory_cases", docenhance::tests::memory_cases);
        run_reference("architecture_cases", docenhance::tests::architecture_cases);
        run_reference("schedule_cases", docenhance::tests::schedule_cases);
        run_reference("box_mean_cases", docenhance::tests::box_mean_cases);
        run_reference("sharpening_cases", docenhance::tests::sharpening_cases);
        run_reference("otsu_cases", docenhance::tests::otsu_cases);
        std::cout << "PASS: parser, numerical and capability reference suites\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
