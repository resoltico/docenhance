// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/methods/binarization.hpp"

#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/sauvola.hpp"
#include "require.hpp"
#include "sauvola_reference.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <variant>

namespace docenhance::tests {
namespace {
constexpr std::size_t memory_limit = std::size_t{4} * 1024 * 1024;
constexpr std::uint8_t untouched = 42;
exec::Scheduler schedule(unsigned workers) {
    return exec::Scheduler{exec::Concurrency::resolve(workers, workers, 0, 0).value()};
}
contract::Invocation request(const std::string& selector) {
    contract::Invocation value;
    value.command = contract::Command::process;
    value.subject = "input.png";
    value.output_directory = "output";
    value.output_mode = "bw";
    value.binarize = selector;
    return value;
}
class SuccessfulProcessor final : public app::Processor {
  public:
    unsigned calls = 0;
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const core::Cancellation& /*cancellation*/) override {
        ++calls;
        return app::PublishedImage{.output = "output/result.png"};
    }
};
void check_samples(image::PlaneView<const std::uint8_t> output, const SauvolaReference& reference) {
    for (std::uint32_t y = 0; y < output.height(); ++y) {
        for (std::uint32_t x = 0; x < output.width(); ++x) {
            require(output.row(y).subspan(x, 1).front() == reference.at(x, y),
                    "B02 reference sample");
        }
    }
}
std::uint8_t singleton_row_reference(image::PlaneView<const std::uint8_t> source, std::uint32_t x,
                                     const methods::Sauvola& method) {
    std::uint64_t sum = 0;
    std::uint64_t squares = 0;
    const auto radius = static_cast<std::int64_t>(method.window() / 2);
    for (auto dx = -radius; dx <= radius; ++dx) {
        const auto column = mirror_coordinate(static_cast<std::int64_t>(x) + dx, source.width());
        const std::uint64_t p = source.row(0).subspan(column, 1).front();
        sum += p * method.window();
        squares += p * p * method.window();
    }
    const auto count = static_cast<std::uint64_t>(method.window()) * method.window();
    const auto n = static_cast<double>(count);
    const auto mean = static_cast<double>(sum) / n;
    const auto deviation = std::sqrt(static_cast<double>((count * squares) - (sum * sum))) / n;
    const auto threshold = mean * (1.0 + (method.k() * ((deviation / (255.0 * method.r())) - 1.0)));
    return source.row(0).subspan(x, 1).front() <= threshold ? 0 : 255;
}
void compare(std::uint32_t width, std::uint32_t height, std::uint32_t window) {
    core::Budget budget{memory_limit};
    auto source = image::Plane<std::uint8_t>::allocate(budget, width, height).value();
    auto output = image::Plane<std::uint8_t>::allocate(budget, width, height).value();
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            source.view().row(y).subspan(x, 1).front() =
                static_cast<std::uint8_t>(((x * 71U) + (y * 113U)) % 256U);
        }
    }
    const auto method = methods::Sauvola::create({.window = window}).value();
    const SauvolaReference reference{
        .source = source.view().as_const(),
        .window = window,
        .k = method.k(),
        .r = method.r(),
    };
    const auto held = budget.used();
    for (const unsigned workers : {1U, 2U, 8U}) {
        const auto scheduler = schedule(workers);
        REQUIRE(methods::sauvola(source.view().as_const(), output.view(), method,
                                 {.scheduler = scheduler, .budget = budget}));
        CHECK(budget.used() == held);
        check_samples(output.view().as_const(), reference);
    }
}
} // namespace
TEST_CASE("Method values cannot contain invalid parameters", "[binarization]") {
    static_assert(!std::is_default_constructible_v<methods::Sauvola>);
    static_assert(!std::is_default_constructible_v<methods::FixedThreshold>);
    static_assert(!std::is_default_constructible_v<methods::Binarization>);
    static_assert(!std::is_constructible_v<methods::Sauvola, std::uint32_t, double, double>);
    for (const auto window : {0U, 1U, 2U, 4U, 4096U, UINT32_MAX}) {
        CHECK(!methods::Sauvola::create({.window = window}));
    }
    for (const double invalid : {
             -0.1,
             1.1,
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN(),
         }) {
        CHECK(!methods::FixedThreshold::create(invalid));
        CHECK(!methods::Sauvola::create({.k = invalid}));
    }
    for (const double r : {
             0.0,
             -1.0,
             1e-300,
             1.1,
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN(),
         }) {
        CHECK(!methods::Sauvola::create({.r = r}));
    }
    REQUIRE(methods::Sauvola::create({.window = 4095, .k = 0, .r = methods::Sauvola::min_r}));
    REQUIRE(methods::Sauvola::create({.window = 3, .k = 1, .r = 1}));
}
TEST_CASE("Application admits only options belonging to the selected method", "[app]") {
    SuccessfulProcessor processor;
    auto value = request("sauvola");
    const auto admitted = app::prepare_process(value);
    REQUIRE(admitted);
    const auto& method =
        std::get<methods::Sauvola>(std::get<methods::Binarization>(admitted->operation()));
    CHECK(method.window() == 31);
    CHECK(method.k() == 0.2);
    CHECK(method.r() == 0.5);
    auto outcome = app::dispatch(value, processor);
    CHECK(std::string(std::get<app::Processed>(outcome.payload).method.id) == "B02");
    CHECK(processor.calls == 1);
    value.fixed_threshold = "";
    CHECK(!app::prepare_process(value));
    value.fixed_threshold = "0.5";
    CHECK(!app::prepare_process(value));
    value = request("fixed");
    value.sauvola_k = "0.2";
    outcome = app::dispatch(value, processor);
    CHECK(std::holds_alternative<app::Failure>(outcome.payload));
    CHECK(processor.calls == 1);
    value.sauvola_k.reset();
    value.sauvola_window = "";
    CHECK(!app::prepare_process(value));
    value.sauvola_window.reset();
    value.sauvola_r = "0.5";
    CHECK(!app::prepare_process(value));
}
TEST_CASE("Window grammar and explicitly empty numeric options are rejected", "[app]") {
    auto value = request("sauvola");
    for (const auto* const spelling :
         {"", "-3", "+3", "3.0", "3e0", " 3", "3 ", "0", "2", "4096", "4294967296"}) {
        value.sauvola_window = spelling;
        CHECK(!app::prepare_process(value));
    }
    value.sauvola_window = "0003";
    REQUIRE(app::prepare_process(value));
    value.sauvola_k = "";
    CHECK(!app::prepare_process(value));
    value.sauvola_k.reset();
    value.sauvola_r = "";
    CHECK(!app::prepare_process(value));
    value = request("fixed");
    value.fixed_threshold = "";
    CHECK(!app::prepare_process(value));
}
TEST_CASE("Sauvola agrees with direct windows across borders and strip seams", "[sauvola]") {
    compare(1, 1, 3);
    compare(1, 11, 31);
    compare(13, 1, 31);
    compare(17, 9, 7);
    compare(19, 11, 31);
    compare(8201, 3, 3);
    compare(4103, 2, 7);
}
TEST_CASE("Maximum windows handle singleton extents and exact equality", "[sauvola]") {
    core::Budget budget{memory_limit};
    auto source = image::Plane<std::uint8_t>::allocate(budget, 1, 3).value();
    auto output = image::Plane<std::uint8_t>::allocate(budget, 1, 3).value();
    const auto scheduler = schedule(8);
    for (const std::uint8_t sample : {std::uint8_t{0}, std::uint8_t{127}, std::uint8_t{255}}) {
        std::ranges::fill(source.view().storage(), sample);
        for (const double k : {0.0, 0.2, 1.0}) {
            const auto method = methods::Sauvola::create({.window = 4095, .k = k}).value();
            REQUIRE(methods::sauvola(source.view().as_const(), output.view(), method,
                                     {.scheduler = scheduler, .budget = budget}));
            CHECK(output.view().row(0).front() == ((k == 0.0 || sample == 0) ? 0 : 255));
        }
    }
}
TEST_CASE("Maximum-radius strip halos agree at every seam", "[sauvola]") {
    core::Budget budget{memory_limit};
    auto source = image::Plane<std::uint8_t>::allocate(budget, 8201, 1).value();
    auto output = image::Plane<std::uint8_t>::allocate(budget, 8201, 1).value();
    for (std::uint32_t x = 0; x < source.width(); ++x) {
        source.view().row(0).subspan(x, 1).front() = static_cast<std::uint8_t>((x * 71U) % 256U);
    }
    const auto method = methods::Sauvola::create({.window = 4095}).value();
    for (const unsigned workers : {1U, 2U, 8U}) {
        const auto scheduler = schedule(workers);
        REQUIRE(methods::sauvola(source.view().as_const(), output.view(), method,
                                 {.scheduler = scheduler, .budget = budget}));
        for (const auto x : {0U, 1U, 4095U, 4096U, 4097U, 8191U, 8192U, 8200U}) {
            require(output.view().row(0).subspan(x, 1).front() ==
                        singleton_row_reference(source.view().as_const(), x, method),
                    "maximum-window seam reference");
        }
    }
}
TEST_CASE("Sauvola reserves exact bounded scratch before writing", "[sauvola]") {
    core::Budget images{memory_limit};
    auto source = image::Plane<std::uint8_t>::allocate(images, 8201, 2).value();
    auto output = image::Plane<std::uint8_t>::allocate(images, 8201, 2).value();
    std::ranges::fill(source.view().storage(), 127);
    std::ranges::fill(output.view().storage(), untouched);
    const auto method = methods::Sauvola::create().value();
    const auto scheduler = schedule(8);
    const auto plan =
        methods::sauvola_workspace(source.width(), method, scheduler.workers()).value();
    CHECK(plan.slots == 3);
    core::Budget refused{plan.bytes - 1};
    const auto result = methods::sauvola(source.view().as_const(), output.view(), method,
                                         {.scheduler = scheduler, .budget = refused});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::resource);
    CHECK(refused.used() == 0);
    CHECK(std::ranges::all_of(output.view().storage(), [](auto p) { return p == untouched; }));
    core::Budget exact{plan.bytes};
    REQUIRE(methods::sauvola(source.view().as_const(), output.view(), method,
                             {.scheduler = scheduler, .budget = exact}));
    CHECK(exact.used() == 0);
    CHECK(!methods::sauvola(source.view().as_const(), source.view(), method,
                            {.scheduler = scheduler, .budget = exact}));
    CHECK(!methods::sauvola({}, output.view(), method, {.scheduler = scheduler, .budget = exact}));
    CHECK(!methods::sauvola_workspace(0, method, 1));
    CHECK(!methods::sauvola_workspace(1, method, 0));
    CHECK(!methods::sauvola_workspace(1, method, 65));
    const auto worst = methods::sauvola_workspace(
                           UINT32_MAX, methods::Sauvola::create({.window = 4095}).value(), 64)
                           .value();
    CHECK(worst.bytes <= std::size_t{8} * 1024 * 1024);
}
} // namespace docenhance::tests
