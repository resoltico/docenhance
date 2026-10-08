// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/restoration.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <utility>
namespace docenhance::tests {
namespace {
contract::Invocation restoration_invocation() {
    contract::Invocation i;
    i.command = contract::Command::process;
    i.subject = "input.png";
    i.output_directory = "output";
    i.deblur = "wiener";
    i.psf = "gaussian";
    return i;
}
using RawMember = std::optional<std::string> contract::Invocation::*;
constexpr std::array private_options{
    &contract::Invocation::psf,          &contract::Invocation::psf_sigma,
    &contract::Invocation::psf_length,   &contract::Invocation::psf_angle,
    &contract::Invocation::psf_file,     &contract::Invocation::wiener_k,
    &contract::Invocation::deblur_blend,
};
} // namespace
TEST_CASE("Restoration admission retains explicit option presence", "[app][restoration]") {
    auto i = restoration_invocation();
    for (const auto* const mode : {"off", ""}) {
        for (const auto member : private_options) {
            for (const auto* const spelling : {"", "0"}) {
                auto refused = i;
                refused.deblur = mode;
                refused.psf.reset();
                refused.*member = spelling;
                CHECK(!app::prepare_process(refused));
            }
        }
    }
    for (const auto member : private_options) {
        auto refused = i;
        refused.output_mode = "bw";
        refused.deblur.reset();
        refused.psf.reset();
        refused.*member = "";
        CHECK(!app::prepare_process(refused));
    }
    i.output_mode = "bw";
    i.deblur = "off";
    i.psf.reset();
    CHECK(!app::prepare_process(i));
}
TEST_CASE("PSF selectors reject incompatible options even at default or empty values",
          "[app][restoration]") {
    for (const auto* const spelling : {"", "1"}) {
        for (const auto member : std::array{
                 &contract::Invocation::psf_length,
                 &contract::Invocation::psf_angle,
                 &contract::Invocation::psf_file,
             }) {
            auto i = restoration_invocation();
            i.*member = spelling;
            CHECK(!app::prepare_process(i));
        }
        for (const auto member : std::array{
                 &contract::Invocation::psf_sigma,
                 &contract::Invocation::psf_file,
             }) {
            auto i = restoration_invocation();
            i.psf = "motion";
            i.*member = spelling;
            CHECK(!app::prepare_process(i));
        }
        for (const auto member : std::array{
                 &contract::Invocation::psf_sigma,
                 &contract::Invocation::psf_length,
                 &contract::Invocation::psf_angle,
             }) {
            auto i = restoration_invocation();
            i.psf = "kernel";
            i.psf_file = "kernel.png";
            i.*member = spelling;
            CHECK(!app::prepare_process(i));
        }
    }
}
TEST_CASE("Restoration kernel path identity survives request moves", "[app][restoration]") {
    auto i = restoration_invocation();
    i.psf = "kernel";
    i.psf_file = "kernels/caf\xc3\xa9.png";
    auto admitted = app::prepare_process(i);
    REQUIRE(admitted);
    auto moved = std::move(*admitted);
    REQUIRE(moved.ready());
    const auto& path =
        std::get<methods::FilePsf>(std::get<methods::Wiener>(moved.restoration()).parameters().psf)
            .path;
    CHECK(path == *i.psf_file);
    auto second = app::prepare_process(restoration_invocation()).value();
    second = std::move(moved);
    CHECK(second.ready());
    CHECK(
        std::get<methods::FilePsf>(std::get<methods::Wiener>(second.restoration()).parameters().psf)
            .path == *i.psf_file);
    for (const auto& invalid :
         {std::string{}, std::string{"bad\0name", 8}, std::string{"bad\xffname"}}) {
        i.psf_file = invalid;
        CHECK(!app::prepare_process(i));
    }
}
TEST_CASE("Restoration admission validates finite bounded values with zero blend",
          "[app][restoration]") {
    for (const auto member : std::array<RawMember, 3>{
             &contract::Invocation::psf_sigma,
             &contract::Invocation::wiener_k,
             &contract::Invocation::deblur_blend,
         }) {
        for (const auto* const spelling : {"", "nan", "inf", "-1", "1e309"}) {
            auto i = restoration_invocation();
            i.*member = spelling;
            CHECK(!app::prepare_process(i));
        }
    }
    auto i = restoration_invocation();
    i.deblur_blend = "0";
    auto admitted = app::prepare_process(i);
    REQUIRE(admitted);
    CHECK(std::get<methods::Wiener>(admitted->restoration()).parameters().blend == 0);
    i.psf.reset();
    CHECK(!app::prepare_process(i));
    i.psf = "kernel";
    CHECK(!app::prepare_process(i));
}
} // namespace docenhance::tests
