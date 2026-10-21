// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "binary_resource_cases.hpp"
#include "docenhance/methods/binarization.hpp"
#include "require.hpp"

#include <catch2/catch_test_macros.hpp>
namespace docenhance::tests {
TEST_CASE("Binary Q16 reserves complete ordered partials and moments before output stores") {
    const auto before = assertion_counter().load();
    for (const auto workers : {1U, 2U, 7U}) {
        binary_resource_case(methods::FixedThreshold::create().value(), 1031, 449, workers);
        binary_resource_case(methods::Otsu::create(), 1031, 449, workers);
        binary_resource_case(methods::Sauvola::create({.window = 4095}).value(), 32777, 2, workers);
    }
    REQUIRE(assertion_counter().load() > before);
}
} // namespace docenhance::tests
