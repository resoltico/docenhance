// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/response_fields.hpp"

#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/response.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/request.hpp"
#include "docenhance/record/schema.hpp"
#include "docenhance/record/source.hpp"
#include "docenhance/report/response.hpp"
#include "execution_fixture.hpp"
#include "png_depth_fixture.hpp"
#include "wire_response_fixture.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace docenhance::tests {
namespace {
app::Outcome discovery(app::Command command) {
    const app::Capabilities capabilities{
        .methods = methods::implemented_methods(),
        .input_support = app::input_support,
    };
    return {
        .command = command,
        .build = core::build_facts(),
        .payload = app::Methods{.capabilities = capabilities},
    };
}
record::RunRecord one_pixel_record() {
    const auto build = core::build_facts();
    const auto method = methods::FixedThreshold::create(0.5);
    REQUIRE(method);
    const methods::Plan plan{
        .rotation = image::QuarterTurn(),
        .photometric = methods::Photometric(),
        .output = methods::BinaryOutput{.method = *method},
    };
    const auto requested = record::requested(plan, std::nullopt);
    REQUIRE(requested);
    record::RunRecord value{
        .context = {.identity = std::string(32, 'a'), .recorded = "2026-10-19T00:00:00Z"},
        .build =
            {
                .version = std::string{build.version},
                .platform = std::string{build.platform},
                .compiler = std::string{build.compiler},
                .dependency_lock_sha256 = std::string{build.dependency_lock_sha256},
            },
        .source =
            {
                .name = "source.png",
                .sha256 = std::string(64, 'b'),
                .bytes = 71,
                .container = record::PngContainer{.width = 1, .height = 1},
            },
        .request = *requested,
        .execution = record::Execution(),
        .assets = std::vector<record::Asset>(),
        .output =
            {
                .sha256 = std::string(64, 'c'),
                .bytes = 71,
                .width = 1,
                .height = 1,
                .depth = docenhance::tests::fixture_png_depth(1),
                .resolution = std::nullopt,
            },
    };
    value.execution.interpretation = {
        .decoded = {.width = 1, .height = 1},
        .output = value.output.shape(),
        .orientation = image::Orientation(),
        .rotation = image::QuarterTurn(),
        .resolution = std::nullopt,
        .assumed_transfer = true,
    };
    methods::FixedFacts facts;
    facts.white_samples = 1;
    value.execution.binarization = facts;
    return value;
}
} // namespace
TEST_CASE("Current discovery responses derive complete catalog values from actual types") {
    auto outcome = discovery(app::Command::methods);
    const auto encoded = wire(outcome);
    REQUIRE(record::Codec::decode<app::WireResponse>(encoded));
    CHECK(encoded["schema_version"] == app::response_schema_version);
    REQUIRE(encoded["version"].is_string());
    CHECK(encoded["version"].get_ref<const std::string&>() == core::build_facts().version);
    CHECK(encoded["methods"].size() == methods::implemented_methods().size());
    auto changed = encoded;
    changed["methods"][0]["id"] = "unimplemented";
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["methods"][0]["method_version"] = 999;
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["input_support"][1]["output_modes"] = {"preserve", "gray"};
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["version"] = "garbage";
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["exit_code"] = 2;
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    for (const auto& method : methods::implemented_methods()) {
        auto selected = outcome;
        selected.payload = app::Methods{
            .capabilities =
                {
                    .methods = std::span<const methods::ImplementedMethod>{&method, 1},
                    .input_support = app::input_support,
                },
        };
        CHECK(record::Codec::decode<app::WireResponse>(wire(selected)));
    }
    outcome.command = app::Command::version;
    outcome.payload = app::Version{
        .capabilities =
            {
                .methods = methods::implemented_methods(),
                .input_support = app::input_support,
            },
    };
    REQUIRE(record::Codec::decode<app::WireResponse>(wire(outcome)));
}
TEST_CASE("Help response fields preserve actual empty metavars and closed command options") {
    auto outcome = discovery(app::Command::root);
    outcome.payload = app::Help{};
    const auto encoded = wire(outcome);
    REQUIRE(record::Codec::decode<app::WireResponse>(encoded));
    auto changed = encoded;
    changed["options"][0].erase("metavar");
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["usage"] = "foreign usage";
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["options"].push_back(changed["options"][0]);
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
}
TEST_CASE("Success response carries the full actual record and separate diagnostics warnings") {
    const auto record = one_pixel_record();
    REQUIRE(record::validate(record));
    const app::Outcome outcome{
        .command = app::Command::process,
        .build = core::build_facts(),
        .payload =
            app::Published{
                .output = "bundle",
                .record_identity = {.sha256 = std::string(64, 'd'), .bytes = 1024},
                .record = record,
                .diagnostics =
                    {
                        .threads = 1,
                        .memory_limit_mib = 1024,
                        .charged_peak_bytes = 4096,
                    },
            },
    };
    const auto encoded = wire(outcome);
    const auto record_value = record::Codec::encode(record);
    REQUIRE(record_value);
    CHECK(encoded["record"] == *record_value);
    CHECK(encoded["record_sha256"] == std::string(64, 'd'));
    CHECK(encoded["warnings"] == record::Json::array({"W_PROFILE_ASSUMED"}));
    CHECK(!encoded["record"].contains("warnings"));
    CHECK(!encoded["record"].contains("publication"));
    REQUIRE(record::Codec::decode<app::WireResponse>(encoded));
    auto changed = encoded;
    changed["record"]["publication"] = "completed";
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["diagnostics"]["charged_peak_bytes"] = std::uint64_t{1024} * 1024 * 1024;
    REQUIRE(record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["diagnostics"]["charged_peak_bytes"] = (std::uint64_t{1024} * 1024 * 1024) + 1;
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    for (const auto memory : {64U, 4096U, 262144U}) {
        changed = encoded;
        changed["diagnostics"]["threads"] = 256;
        changed["diagnostics"]["memory_limit_mib"] = memory;
        changed["diagnostics"]["charged_peak_bytes"] = std::uint64_t{memory} * 1024 * 1024;
        REQUIRE(record::Codec::decode<app::WireResponse>(changed));
        changed["diagnostics"]["charged_peak_bytes"] = (std::uint64_t{memory} * 1024 * 1024) + 1;
        CHECK(!record::Codec::decode<app::WireResponse>(changed));
    }
    for (const auto threads : {0U, 257U}) {
        changed = encoded;
        changed["diagnostics"]["threads"] = threads;
        CHECK(!record::Codec::decode<app::WireResponse>(changed));
    }
    for (const auto memory : {63U, 262145U}) {
        changed = encoded;
        changed["diagnostics"]["memory_limit_mib"] = memory;
        CHECK(!record::Codec::decode<app::WireResponse>(changed));
    }
    changed = encoded;
    changed["record_sha256"] = std::string(64, 'B');
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
}
TEST_CASE("Failure wire keeps explicit effects and excludes private runtime warning context") {
    app::ProcessFailure failure{
        .error = {.code = core::ErrorCode::publication_unknown, .message = "unknown"},
        .publication = core::Publication::unknown,
    };
    const app::Outcome outcome{
        .command = app::Command::process,
        .build = core::build_facts(),
        .payload = failure,
    };
    const auto encoded = wire(outcome);
    CHECK(encoded["publication"] == "unknown");
    CHECK(encoded["exit_code"] == 7);
    CHECK(encoded["diagnostics"].is_null());
    CHECK(encoded["completed"].empty());
    CHECK(!encoded.contains("warning_context"));
    REQUIRE(record::Codec::decode<app::WireResponse>(encoded));
    auto changed = encoded;
    changed["publication"] = "not_started";
    CHECK(record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["command"] = "verify";
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["exit_code"] = 5;
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["failed_stage"] = "foreign";
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    auto nul = outcome;
    std::get<app::Failure>(nul.payload).error.message = std::string{"a\0b", 3};
    const auto safe = wire(nul);
    CHECK(safe["error"]["message"] == "The diagnostic contained a NUL character");
    CHECK(safe["publication"] == "unknown");
    CHECK(record::Codec::decode<app::WireResponse>(safe));
    failure.error = {.code = core::ErrorCode::output_verify, .message = "readback"};
    failure.publication = core::Publication::completed;
    auto committed = outcome;
    committed.payload = failure;
    CHECK(record::Codec::decode<app::WireResponse>(wire(committed)));
    changed = wire(committed);
    changed["failed_stage"] = "restoration";
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    app::ResponseFailure reversed;
    reversed.failed_stage = methods::Family::illumination;
    methods::FixedFacts later;
    later.white_samples = 1;
    REQUIRE(methods::validate(later));
    reversed.completed.binarization = later;
    CHECK(!app::validate(reversed));
}
TEST_CASE("Root sharpening warning derives from actual completed positive or zero amount facts") {
    for (const auto* const output_mode : {"preserve", "bw"}) {
        for (const auto* const amount : {"0", "0.5"}) {
            app::Invocation invocation;
            invocation.command = app::Command::process;
            invocation.operands = {"input.png"};
            invocation.option(app::OptionId::out_dir) = "bundle";
            invocation.option(app::OptionId::output_mode) = output_mode;
            if (std::string_view{output_mode} == "bw") {
                invocation.option(app::OptionId::binarize) = "fixed";
            }
            invocation.option(app::OptionId::sharpen) = "unsharp";
            invocation.option(app::OptionId::sharpen_amount) = amount;
            const auto published = observed_publication(invocation);
            REQUIRE(published.record.execution.sharpening.has_value());
            const app::Outcome outcome{
                .command = app::Command::process,
                .build = core::build_facts(),
                .payload = published,
            };
            const auto encoded = wire(outcome);
            const auto warnings = encoded.at("warnings").get<std::vector<std::string>>();
            CHECK(std::ranges::count(warnings, "W_SHARPENING") ==
                  (std::string_view{amount} == "0" ? 0 : 1));
            CHECK(!encoded.at("record").contains("warnings"));
            CHECK(!encoded.at("record").at("execution").at("sharpening").contains("warnings"));
            CHECK(record::Codec::decode<app::WireResponse>(encoded));
        }
    }
}
} // namespace docenhance::tests

namespace docenhance::tests {
TEST_CASE("Failure receivers retain cancellation effects and read-only absence of work") {
    const app::Outcome outcome{
        .command = app::Command::process,
        .build = core::build_facts(),
        .payload =
            app::ProcessFailure{
                .error = {.code = core::ErrorCode::cancelled, .message = "cancelled"},
                .publication = core::Publication::not_started,
            },
    };
    const auto healthy = wire(outcome);
    for (const auto* const publication : {"not_started", "not_published"}) {
        auto changed = healthy;
        changed["publication"] = publication;
        CHECK(record::Codec::decode<app::WireResponse>(changed));
    }
    for (const auto* const publication : {"completed", "unknown"}) {
        auto changed = healthy;
        changed["publication"] = publication;
        CHECK(!record::Codec::decode<app::WireResponse>(changed));
    }
    auto readonly = healthy;
    readonly["command"] = "verify";
    REQUIRE(record::Codec::decode<app::WireResponse>(readonly));
    for (const auto* const publication : {"not_published", "completed", "unknown"}) {
        auto changed = readonly;
        changed["publication"] = publication;
        CHECK(!record::Codec::decode<app::WireResponse>(changed));
    }
    readonly["failed_stage"] = "illumination";
    CHECK(!record::Codec::decode<app::WireResponse>(readonly));
}
TEST_CASE("Shared version schema preserves Text while typed responses bind actual native build") {
    auto outcome = discovery(app::Command::version);
    const auto capabilities = std::get<app::Methods>(outcome.payload).capabilities;
    outcome.payload = app::Version{.capabilities = capabilities};
    const auto encoded = wire(outcome);
    REQUIRE(record::Codec::decode<app::WireResponse>(encoded));
    for (const auto* const field : {"platform", "compiler"}) {
        auto changed = encoded;
        changed[field] = encoded.at(field).get<std::string>() + "-foreign";
        CHECK(!record::Codec::decode<app::WireResponse>(changed));
    }
    const auto schema = report::response_schema();
    const auto typed =
        record::Schema::of<app::Response<app::Command::version, app::ResponseVersion>>();
    REQUIRE(schema);
    REQUIRE(typed);
    if (!schema || !typed) {
        throw std::runtime_error("Actual response schemas are required");
    }
    bool found = false;
    for (const auto& branch : schema.value().at("oneOf")) {
        const auto& properties = branch.at("properties");
        if (properties.contains("platform")) {
            found = true;
            for (const auto* const field : {"platform", "compiler"}) {
                CHECK(properties.at(field) == typed.value().at("properties").at(field));
                CHECK(!properties.at(field).contains("const"));
            }
            CHECK(properties.at("dependency_lock_sha256").contains("const"));
            CHECK(properties.at("version").contains("const"));
            CHECK(properties.at("methods").contains("const"));
        }
    }
    CHECK(found);
}

} // namespace docenhance::tests
