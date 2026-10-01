// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "architecture_cases.hpp"
#include "docenhance/contract/parse.hpp"
#include "pipeline_cases.hpp"
#include "reference_cases.hpp"

#include <catch2/catch_test_macros.hpp>
#include <clocale>
#include <string>

namespace {
class NumericLocale {
  public:
    // NOLINTNEXTLINE(concurrency-mt-unsafe): Serial locale test.
    NumericLocale() : original_(std::setlocale(LC_NUMERIC, nullptr)) {
        // Availability is platform-specific; macOS provides the French UTF-8 locale.
        for (const auto* const name : {"fr_FR.UTF-8", "de_DE.UTF-8", "French_France.1252"}) {
            // NOLINTNEXTLINE(concurrency-mt-unsafe): Serial locale test.
            if (std::setlocale(LC_NUMERIC, name) != nullptr) {
                break;
            }
        }
    }
    ~NumericLocale() {
        // NOLINTNEXTLINE(concurrency-mt-unsafe): Serial locale test.
        static_cast<void>(std::setlocale(LC_NUMERIC, original_.c_str()));
    }
    NumericLocale(const NumericLocale&) = delete;
    NumericLocale& operator=(const NumericLocale&) = delete;
    NumericLocale(NumericLocale&&) = delete;
    NumericLocale& operator=(NumericLocale&&) = delete;

  private:
    std::string original_;
};
} // namespace

TEST_CASE("Decimal admission keeps its grammar in the available process numeric locale", "[spec]") {
    const NumericLocale locale;
    REQUIRE(docenhance::contract::parse_finite("1.5", 0, 2).value() == 1.5);
    REQUIRE(!docenhance::contract::parse_finite("1,5", 0, 2));
}

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

TEST_CASE("Ownership, views, execution boundaries and admitted requests", "[architecture]") {
    REQUIRE_NOTHROW(docenhance::tests::architecture_cases());
}
