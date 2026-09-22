// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "foundation_cases.hpp"
#include "pipeline_cases.hpp"
#include "reference_cases.hpp"

#include <catch2/catch_test_macros.hpp>
TEST_CASE("Strict parsers and checked arithmetic", "[spec]") {
    REQUIRE_NOTHROW(docenhance::tests::parser_cases());
}
TEST_CASE("Decimal grammar, equivalent spellings and option ranges", "[spec]") {
    REQUIRE_NOTHROW(docenhance::tests::decimal_cases());
}
TEST_CASE("Numerical reference primitives", "[numeric]") {
    REQUIRE_NOTHROW(docenhance::tests::numerical_cases());
}
TEST_CASE("Unimplemented methods are not advertised", "[capabilities]") {
    REQUIRE_NOTHROW(docenhance::tests::capabilities_cases());
}
TEST_CASE("Memory is budgeted, aligned and owned") {
    REQUIRE_NOTHROW(docenhance::tests::memory_cases());
}
TEST_CASE("The schedule is bounded, deterministic and reports the earliest failure") {
    REQUIRE_NOTHROW(docenhance::tests::schedule_cases());
}
TEST_CASE("The box mean agrees with its definition and with itself") {
    REQUIRE_NOTHROW(docenhance::tests::box_mean_cases());
}

TEST_CASE("Ownership, views, execution boundaries and admitted requests", "[foundation]") {
    REQUIRE_NOTHROW(docenhance::tests::foundation_cases());
}
