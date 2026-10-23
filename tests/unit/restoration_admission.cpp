// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/restoration.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
namespace docenhance::tests {
namespace {
app::Invocation restoration_invocation() {
    app::Invocation i;
    i.command = app::Command::process;
    i.operands = {"input.png"};
    i.option(app::OptionId::out_dir) = "output";
    i.option(app::OptionId::deblur) = "wiener";
    i.option(app::OptionId::psf) = "gaussian";
    return i;
}
using RawOption = app::OptionId;
constexpr std::array private_options{
    app::OptionId::psf,          app::OptionId::psf_sigma, app::OptionId::psf_length,
    app::OptionId::psf_angle,    app::OptionId::psf_file,  app::OptionId::wiener_k,
    app::OptionId::deblur_blend,
};
} // namespace
TEST_CASE("Restoration admission retains explicit option presence", "[app][restoration]") {
    auto i = restoration_invocation();
    for (const auto* const mode : {"off", ""}) {
        for (const auto member : private_options) {
            for (const auto* const spelling : {"", "0"}) {
                auto refused = i;
                refused.option(app::OptionId::deblur) = mode;
                refused.option(app::OptionId::psf).reset();
                refused.option(member) = spelling;
                CHECK(!app::prepare_process(refused));
            }
        }
    }
    for (const auto member : private_options) {
        auto refused = i;
        refused.option(app::OptionId::output_mode) = "bw";
        refused.option(app::OptionId::deblur).reset();
        refused.option(app::OptionId::psf).reset();
        refused.option(member) = "";
        CHECK(!app::prepare_process(refused));
    }
    i.option(app::OptionId::output_mode) = "bw";
    i.option(app::OptionId::deblur) = "off";
    i.option(app::OptionId::psf).reset();
    const auto binary_off = app::prepare_process(i);
    REQUIRE(binary_off);
    CHECK(std::holds_alternative<methods::BinaryOutput>(binary_off->plan().output));
    CHECK(std::holds_alternative<methods::RestorationOff>(
        binary_off->plan().photometric.restoration));
    i.option(app::OptionId::deblur) = "wiener";
    i.option(app::OptionId::psf) = "gaussian";
    const auto binary_selected = app::prepare_process(i);
    REQUIRE(binary_selected);
    CHECK(std::holds_alternative<methods::BinaryOutput>(binary_selected->plan().output));
    CHECK(std::holds_alternative<methods::Wiener>(binary_selected->plan().photometric.restoration));
}
TEST_CASE("PSF selectors reject incompatible options even at default or empty values",
          "[app][restoration]") {
    for (const auto* const spelling : {"", "1"}) {
        for (const auto member : std::array{
                 app::OptionId::psf_length,
                 app::OptionId::psf_angle,
                 app::OptionId::psf_file,
             }) {
            auto i = restoration_invocation();
            i.option(member) = spelling;
            CHECK(!app::prepare_process(i));
        }
        for (const auto member : std::array{
                 app::OptionId::psf_sigma,
                 app::OptionId::psf_file,
             }) {
            auto i = restoration_invocation();
            i.option(app::OptionId::psf) = "motion";
            i.option(member) = spelling;
            CHECK(!app::prepare_process(i));
        }
        for (const auto member : std::array{
                 app::OptionId::psf_sigma,
                 app::OptionId::psf_length,
                 app::OptionId::psf_angle,
             }) {
            auto i = restoration_invocation();
            i.option(app::OptionId::psf) = "kernel";
            i.option(app::OptionId::psf_file) = "kernel.png";
            i.option(member) = spelling;
            CHECK(!app::prepare_process(i));
        }
    }
}
TEST_CASE("Restoration kernel path identity survives request moves", "[app][restoration]") {
    auto i = restoration_invocation();
    i.option(app::OptionId::psf) = "kernel";
    i.option(app::OptionId::psf_file) = "kernels/caf\xc3\xa9.png";
    auto admitted = app::prepare_process(i);
    REQUIRE(admitted);
    auto moved = std::move(*admitted);
    REQUIRE(moved.ready());
    const auto expected_path = i.option(app::OptionId::psf_file);
    REQUIRE(expected_path);
    if (!expected_path.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    REQUIRE(moved.psf_path());
    const auto& moved_path = moved.psf_path();
    if (!moved_path.has_value()) {
        throw std::logic_error("Required request PSF path is absent");
    }
    CHECK(moved_path.value() == expected_path.value());
    CHECK(!admitted->ready());
    CHECK(std::holds_alternative<methods::Kernel>(
        std::get<methods::Wiener>(moved.plan().photometric.restoration).parameters().psf));
    auto another = app::prepare_process(restoration_invocation());
    REQUIRE(another);
    auto second = std::move(*another);
    second = std::move(moved);
    CHECK(second.ready());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(!moved.ready());
    REQUIRE(second.psf_path());
    const auto& second_path = second.psf_path();
    if (!second_path.has_value()) {
        throw std::logic_error("Required request PSF path is absent");
    }
    CHECK(second_path.value() == expected_path.value());
    for (const auto& invalid :
         {std::string{}, std::string{"bad\0name", 8}, std::string{"bad\xffname"}}) {
        i.option(app::OptionId::psf_file) = invalid;
        CHECK(!app::prepare_process(i));
    }
}
TEST_CASE("Restoration admission validates finite bounded values with zero blend",
          "[app][restoration]") {
    for (const auto member : std::array<RawOption, 3>{
             app::OptionId::psf_sigma,
             app::OptionId::wiener_k,
             app::OptionId::deblur_blend,
         }) {
        for (const auto* const spelling : {"", "nan", "inf", "-1", "1e309"}) {
            auto i = restoration_invocation();
            i.option(member) = spelling;
            CHECK(!app::prepare_process(i));
        }
    }
    auto i = restoration_invocation();
    i.option(app::OptionId::deblur_blend) = "0";
    auto admitted = app::prepare_process(i);
    REQUIRE(admitted);
    CHECK(std::get<methods::Wiener>(admitted->plan().photometric.restoration).parameters().blend ==
          0);
    i.option(app::OptionId::psf).reset();
    CHECK(!app::prepare_process(i));
    i.option(app::OptionId::psf) = "kernel";
    CHECK(!app::prepare_process(i));
}
} // namespace docenhance::tests
