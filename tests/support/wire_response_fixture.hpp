// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/dispatch.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/report/response.hpp"

#include <catch2/catch_test_macros.hpp>
#include <utility>
namespace docenhance::tests {
inline record::Json wire(const app::Outcome& value) {
    const auto response = report::response_value(value);
    REQUIRE(response);
    auto encoded = report::encode_response(*response);
    REQUIRE(encoded);
    return std::move(*encoded);
}
} // namespace docenhance::tests
