// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/binarization.hpp"
#include "processor.hpp"
#include "stub_verifier.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
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
    for (const auto& input : payload->capabilities.input_support) {
        CHECK(input.binary);
    }
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
        std::get<methods::BinaryOutput>(admitted->plan().output).method));
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
    CHECK(app::prepare_process(value)->plan().rotation.degrees() == 0);
    for (const auto degrees : {0U, 90U, 180U, 270U}) {
        value.option(app::OptionId::rotate) = std::to_string(degrees);
        const auto admitted = app::prepare_process(value);
        REQUIRE(admitted);
        CHECK(admitted->plan().rotation.degrees() == degrees);
        value.option(app::OptionId::output_mode) = "bw";
        REQUIRE(app::prepare_process(value));
        CHECK(app::prepare_process(value)->plan().rotation.degrees() == degrees);
        value.option(app::OptionId::output_mode).reset();
    }
    for (const auto* const spelling :
         {"", "00", "090", "90.0", "9e1", "+90", "-90", "360", " 90", "90 "}) {
        value.option(app::OptionId::rotate) = spelling;
        CHECK(!app::prepare_process(value));
    }
}
TEST_CASE("Execution controls admit bounded workers and whole MiB budgets independently of plans") {
    constexpr auto mib = std::uint64_t{1024} * 1024;
    const app::ExecutionControl defaults;
    CHECK(!defaults.workers().has_value());
    CHECK(defaults.memory_limit() == core::default_memory_limit);
    for (const auto workers : {1U, 2U, 7U, 256U}) {
        for (const auto memory : {64U, 1024U, 4096U, 262144U}) {
            const auto control = app::ExecutionControl::create(workers, memory * mib);
            REQUIRE(control);
            CHECK(control.value().workers() == workers);
            CHECK(control.value().memory_limit() == memory * mib);
        }
    }
    CHECK(!app::ExecutionControl::create(0));
    CHECK(!app::ExecutionControl::create(257));
    CHECK(!app::ExecutionControl::create({}, 63 * mib));
    CHECK(!app::ExecutionControl::create({}, 262145 * mib));
    CHECK(!app::ExecutionControl::create({}, (64 * mib) + 1));
    CHECK(!app::ExecutionControl::create({}, std::numeric_limits<std::uint64_t>::max()));
    std::stop_source stop;
    const auto control =
        app::ExecutionControl::create(7, 64 * mib, core::Cancellation{stop.get_token()});
    REQUIRE(control);
    CHECK(!control.value().cancellation().requested(core::Checkpoint::admission));
    REQUIRE(request_stop(stop));
    CHECK(control.value().cancellation().requested(core::Checkpoint::admission));
}
TEST_CASE("Execution option admission retains integer grammar and prevents processing effects") {
    app::Invocation invocation;
    invocation.command = app::Command::process;
    invocation.operands = {"input.png"};
    invocation.option(app::OptionId::out_dir) = "output";
    for (const auto* const raw : {"auto", "1", "007", "256"}) {
        invocation.option(app::OptionId::threads) = raw;
        REQUIRE(app::prepare_process(invocation));
        const auto control = app::prepare_execution(invocation);
        REQUIRE(control);
        CHECK(control.value().workers().has_value() == (std::string_view{raw} != "auto"));
    }
    for (const auto* const raw : {"64", "001024", "4096", "262144"}) {
        invocation.option(app::OptionId::memory_limit) = raw;
        REQUIRE(app::prepare_process(invocation));
        REQUIRE(app::prepare_execution(invocation));
    }
    for (const auto id : {app::OptionId::threads, app::OptionId::memory_limit}) {
        for (const auto* const raw :
             {"", "0", "+1", "-1", "1.0", "1e2", " 64", "64 ", "4294967296"}) {
            invocation.option(app::OptionId::threads) = "auto";
            invocation.option(app::OptionId::memory_limit) = "1024";
            invocation.option(id) = raw;
            RejectingProcessor processor;
            const auto outcome = app::dispatch(invocation, processor, verifier);
            CHECK(outcome.exit_code() == core::ExitCode::invocation);
            CHECK(processor.calls == 0);
        }
    }
    invocation.command = app::Command::verify;
    invocation.operands = {"bundle"};
    invocation.option(app::OptionId::out_dir).reset();
    invocation.option(app::OptionId::threads) = "1";
    invocation.option(app::OptionId::memory_limit).reset();
    RejectingProcessor processor;
    CHECK(app::dispatch(invocation, processor, verifier).exit_code() == core::ExitCode::invocation);
    CHECK(processor.calls == 0);
}
TEST_CASE("Dispatch passes execution settings separately from the admitted processing plan") {
    app::Invocation invocation;
    invocation.command = app::Command::process;
    invocation.operands = {"input.png"};
    invocation.option(app::OptionId::out_dir) = "output";
    invocation.option(app::OptionId::threads) = "7";
    invocation.option(app::OptionId::memory_limit) = "64";
    RejectingProcessor processor;
    const auto outcome = app::dispatch(invocation, processor, verifier);
    CHECK(outcome.exit_code() == core::ExitCode::processing);
    REQUIRE(processor.calls == 1);
    REQUIRE(processor.observed.has_value());
    if (!processor.observed.has_value()) {
        throw std::logic_error("Required execution settings are absent");
    }
    CHECK(processor.observed.value().workers() == 7);
    CHECK(processor.observed.value().memory_limit() == std::uint64_t{64} * 1024 * 1024);
}
} // namespace docenhance::tests
