// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/binarization.hpp"
#include "processor.hpp"
#include "stub_verifier.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <variant>

namespace docenhance::tests {
namespace {
UnusedVerifier verifier;
} // namespace
namespace {
const app::Failure& failure_for(const app::Outcome& outcome) {
    const auto* failure = std::get_if<app::Failure>(&outcome.payload);
    REQUIRE(failure != nullptr);
    return *failure;
}
} // namespace

TEST_CASE("Application owns invocation requirements", "[app]") {
    RejectingProcessor processor;
    app::Invocation process{};
    process.command = app::Command::process;
    CHECK(failure_for(app::dispatch(process, processor, verifier)).error.code ==
          core::ErrorCode::argument);

    process.operands = {"input.jpg"};
    const auto output = app::dispatch(process, processor, verifier);
    const auto& output_required = failure_for(output);
    CHECK(output_required.error.code == core::ErrorCode::argument);
    CHECK(output_required.error.message.contains("--out-dir"));
    CHECK(output_required.error.message.contains("required"));

    process.option(app::OptionId::out_dir) = "results";
    CHECK(
        std::holds_alternative<app::Failure>(app::dispatch(process, processor, verifier).payload));
}

TEST_CASE("Application owns capability discovery", "[app]") {
    RejectingProcessor processor;
    app::Invocation version{};
    version.command = app::Command::version;
    const auto outcome = app::dispatch(version, processor, verifier);
    const auto* payload = std::get_if<app::Version>(&outcome.payload);
    REQUIRE(payload != nullptr);
    CHECK(payload->capabilities.methods.size() == 12);
    CHECK(std::string{payload->capabilities.methods.front().id} == "I01");
    CHECK(payload->capabilities.input_support.size() == 3);
    CHECK(std::string{payload->capabilities.input_support.front().format} == "png");
    CHECK(std::string{payload->capabilities.input_support.back().format} == "tiff");
}
TEST_CASE("Otsu admission rejects every explicit method-specific option", "[app][otsu]") {
    app::Invocation value;
    value.command = app::Command::process;
    value.operands = {"input.png"};
    value.option(app::OptionId::out_dir) = "output";
    value.option(app::OptionId::output_mode) = "bw";
    value.option(app::OptionId::binarize) = "otsu";
    const auto admitted = app::prepare_process(value);
    REQUIRE(admitted);
    CHECK(std::holds_alternative<methods::Otsu>(
        std::get<app::BinaryPlan>(admitted->plan()).operation));
    for (unsigned option = 0; option < 4; ++option) {
        for (const auto* const spelling : {"", "0.5"}) {
            auto changed = value;
            if (option == 0) {
                changed.option(app::OptionId::fixed_threshold) = spelling;
            } else if (option == 1) {
                changed.option(app::OptionId::sauvola_window) = spelling;
            } else if (option == 2) {
                changed.option(app::OptionId::sauvola_k) = spelling;
            } else {
                changed.option(app::OptionId::sauvola_r) = spelling;
            }
            CHECK(!app::prepare_process(changed));
        }
    }
}
TEST_CASE("Quarter-turn admission accepts only its explicit degree spellings", "[app][geometry]") {
    app::Invocation value;
    value.command = app::Command::process;
    value.operands = {"input.png"};
    value.option(app::OptionId::out_dir) = "output";
    REQUIRE(app::prepare_process(value));
    CHECK(app::prepare_process(value)->rotation().degrees() == 0);
    for (const auto degrees : {0U, 90U, 180U, 270U}) {
        value.option(app::OptionId::rotate) = std::to_string(degrees);
        const auto admitted = app::prepare_process(value);
        REQUIRE(admitted);
        CHECK(admitted->rotation().degrees() == degrees);
        value.option(app::OptionId::output_mode) = "bw";
        REQUIRE(app::prepare_process(value));
        CHECK(app::prepare_process(value)->rotation().degrees() == degrees);
        value.option(app::OptionId::output_mode).reset();
    }
    for (const auto* const spelling :
         {"", "00", "090", "90.0", "9e1", "+90", "-90", "360", " 90", "90 "}) {
        value.option(app::OptionId::rotate) = spelling;
        CHECK(!app::prepare_process(value));
    }
}
} // namespace docenhance::tests
