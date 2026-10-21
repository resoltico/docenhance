// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "binary_control_cases.hpp"
#include "docenhance/methods/binarization.hpp"
#include "require.hpp"

#include <catch2/catch_test_macros.hpp>
namespace docenhance::tests {
TEST_CASE("Binary Q16 fixed bands and logical strips report actual1 2 7 participation") {
    const auto before = assertion_counter().load();
    binary_worker_case(methods::FixedThreshold::create().value(), 1031, 449);
    binary_worker_case(methods::Otsu::create(), 1031, 449);
    binary_worker_case(methods::Sauvola::create({.window = 7}).value(), 32777, 2);
    binary_worker_case(methods::Otsu::create(), 3, 1);
    REQUIRE(assertion_counter().load() > before);
}
TEST_CASE("Binary Q16 cancellation reaches actual phases and preserves real partial prefixes") {
    const auto before = assertion_counter().load();
    const auto fixed = methods::FixedThreshold::create().value();
    const auto otsu = methods::Otsu::create();
    const auto sauvola = methods::Sauvola::create({.window = 7}).value();
    binary_phase_cases(fixed);
    binary_phase_cases(otsu);
    binary_phase_cases(sauvola);
    binary_prefix_case(fixed);
    binary_prefix_case(otsu);
    binary_prefix_case(sauvola);
    REQUIRE(assertion_counter().load() > before);
}
TEST_CASE("Binary Q16 exact next-band final-partial-band and final-fold stores are attained") {
    const auto before = assertion_counter().load();
    binary_boundary_cases(methods::FixedThreshold::create().value());
    binary_boundary_cases(methods::Otsu::create());
    binary_boundary_cases(methods::Sauvola::create({.window = 7}).value());
    REQUIRE(assertion_counter().load() > before);
}
TEST_CASE("Binary Q16 actual2 7 cancellation retains correct partial stores and joined refunds") {
    const auto before = assertion_counter().load();
    binary_multislot_cases(methods::FixedThreshold::create().value());
    binary_multislot_cases(methods::Otsu::create());
    binary_multislot_cases(methods::Sauvola::create({.window = 7}).value());
    REQUIRE(assertion_counter().load() > before);
}
} // namespace docenhance::tests
