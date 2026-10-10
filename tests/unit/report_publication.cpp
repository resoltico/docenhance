// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/report/render.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>

namespace docenhance::tests {
TEST_CASE("Human failures preserve publication state independently of error code", "[report]") {
    contract::Invocation invocation;
    invocation.command = contract::Command::process;
    struct FailureCase {
        core::Publication state;
        core::ErrorCode code;
        std::string_view name;
        std::string_view identifier;
    };
    const auto cases = std::to_array<FailureCase>({
        {
            .state = core::Publication::not_started,
            .code = core::ErrorCode::input,
            .name = "not_started",
            .identifier = "E_INPUT",
        },
        {
            .state = core::Publication::not_published,
            .code = core::ErrorCode::output_verify,
            .name = "not_published",
            .identifier = "E_OUTPUT_VERIFY",
        },
        {
            .state = core::Publication::completed,
            .code = core::ErrorCode::output_verify,
            .name = "completed",
            .identifier = "E_OUTPUT_VERIFY",
        },
        {
            .state = core::Publication::unknown,
            .code = core::ErrorCode::publication_unknown,
            .name = "unknown",
            .identifier = "E_PUBLICATION_UNKNOWN",
        },
    });
    for (const auto& [state, code, name, identifier] : cases) {
        // E_OUTPUT_VERIFY covers both sides of commit; the code alone cannot decide recovery.
        const auto outcome = app::failure(
            invocation, {.code = code, .message = "Observed failure", .publication = state});
        const auto text = report::render(outcome, report::Format::text);
        REQUIRE(text.out.empty());
        REQUIRE(text.err.starts_with(std::string(identifier) + ": Observed failure\n"));
        REQUIRE(text.err.contains("Publication: " + std::string(name) + "\n"));
        REQUIRE(text.err.contains("incomplete response or delivery failure"));
        REQUIRE(!text.err.contains("Execution did not start"));
        const auto json = report::render(outcome, report::Format::json);
        REQUIRE(json.err.empty());
        REQUIRE(json.out.contains("\"publication\": \"" + std::string(name) + "\""));
        if (state == core::Publication::completed || state == core::Publication::unknown) {
            REQUIRE(text.err.contains("inspect"));
            REQUIRE(text.err.contains("Do not retry blindly"));
            REQUIRE(!text.err.contains("before retrying"));
        } else {
            REQUIRE(text.err.contains("correct the reported problem before retrying"));
        }
        if (state == core::Publication::completed) {
            REQUIRE(text.err.contains("docenhance verify DIRECTORY"));
        }
        if (state == core::Publication::unknown) {
            REQUIRE(text.err.contains("staging entries"));
        }
    }
}
} // namespace docenhance::tests
