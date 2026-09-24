// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/dispatch.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "processor.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
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
    RejectingProcessor processor;
    contract::Invocation process{};
    process.command = contract::Command::process;
    CHECK(failure_for(app::dispatch(process, processor)).error.code == core::ErrorCode::argument);

    process.subject = "input.jpg";
    const auto output = app::dispatch(process, processor);
    const auto& output_required = failure_for(output);
    CHECK(output_required.error.code == core::ErrorCode::argument);
    CHECK(output_required.error.message == "--out-dir is required");

    process.output_directory = "results";
    CHECK(std::holds_alternative<app::Failure>(app::dispatch(process, processor).payload));
}

TEST_CASE("Application owns capability discovery", "[app]") {
    RejectingProcessor processor;
    contract::Invocation version{};
    version.command = contract::Command::version;
    const auto outcome = app::dispatch(version, processor);
    const auto* payload = std::get_if<app::Version>(&outcome.payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->capabilities.methods.size() == 2);
    CHECK(std::string{payload->capabilities.methods.front().id} == "B02");
    CHECK(payload->capabilities.input_formats.size() == 1);
    CHECK(std::string{payload->capabilities.input_formats.front()} == "png");
}
} // namespace docenhance::tests
