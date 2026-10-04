// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "instant.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

namespace docenhance::tests {
namespace {
constexpr std::int64_t seconds_per_day = 86400;
} // namespace

TEST_CASE("An instant is written as RFC 3339 in UTC", "[instant]") {
    CHECK(host::rfc3339_utc(0) == "1970-01-01T00:00:00Z");
    CHECK(host::rfc3339_utc(1) == "1970-01-01T00:00:01Z");
    CHECK(host::rfc3339_utc(seconds_per_day - 1) == "1970-01-01T23:59:59Z");
    CHECK(host::rfc3339_utc(seconds_per_day) == "1970-01-02T00:00:00Z");
    // Each field is placed, not merely counted: 13:46:40 on the 1,000,000,000th second.
    CHECK(host::rfc3339_utc(1000000000) == "2001-09-09T01:46:40Z");
}

TEST_CASE("The civil date follows the calendar's own rules", "[instant]") {
    SECTION("a leap day exists in a year divisible by four") {
        CHECK(host::rfc3339_utc(1709164800) == "2024-02-29T00:00:00Z");
        CHECK(host::rfc3339_utc(1709164800 + seconds_per_day) == "2024-03-01T00:00:00Z");
    }
    SECTION("a century is not a leap year unless it divides by four hundred") {
        // 1900 had no 29 February; 2000 did.
        CHECK(host::rfc3339_utc(-2203977600) == "1900-02-28T00:00:00Z");
        CHECK(host::rfc3339_utc(-2203977600 + seconds_per_day) == "1900-03-01T00:00:00Z");
        CHECK(host::rfc3339_utc(951782400) == "2000-02-29T00:00:00Z");
    }
    SECTION("a year ends and begins") {
        CHECK(host::rfc3339_utc(1735689599) == "2024-12-31T23:59:59Z");
        CHECK(host::rfc3339_utc(1735689600) == "2025-01-01T00:00:00Z");
    }
    SECTION("an instant before the epoch is a date before it") {
        CHECK(host::rfc3339_utc(-1) == "1969-12-31T23:59:59Z");
        CHECK(host::rfc3339_utc(-seconds_per_day) == "1969-12-31T00:00:00Z");
    }
    SECTION("the representable range is written at full width") {
        CHECK(host::rfc3339_utc(-62167219200) == "0000-01-01T00:00:00Z");
        CHECK(host::rfc3339_utc(253402300799) == "9999-12-31T23:59:59Z");
    }
}
} // namespace docenhance::tests
