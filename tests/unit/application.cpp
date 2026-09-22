// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/dispatch.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"

#include <catch2/catch_test_macros.hpp>
#include <variant>

namespace docenhance::tests {
namespace {
const app::Failure& failure_for(const app::Outcome& outcome) {
    const auto* failure = std::get_if<app::Failure>(&outcome.payload);
    REQUIRE(failure != nullptr);
    return *failure;
}
} // namespace

TEST_CASE("Application owns invocation requirements", "[app]") {
    contract::Invocation process{.command = contract::Command::process};
    CHECK(failure_for(app::dispatch(process)).error.code == core::ErrorCode::argument);

    process.subject = "input.jpg";
    const auto& output_required = failure_for(app::dispatch(process));
    CHECK(output_required.error.code == core::ErrorCode::argument);
    CHECK(output_required.error.message == "--out-dir is required");

    process.output_directory = "results";
    CHECK(std::holds_alternative<app::Failure>(app::dispatch(process).payload));
}

TEST_CASE("Application owns capability discovery", "[app]") {
    const contract::Invocation version{.command = contract::Command::version};
    const auto outcome = app::dispatch(version);
    const auto* payload = std::get_if<app::Version>(&outcome.payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->capabilities.methods.empty());
    CHECK(payload->capabilities.input_formats.empty());
}
} // namespace docenhance::tests
