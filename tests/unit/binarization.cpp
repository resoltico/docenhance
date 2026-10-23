// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/binarization.hpp"

#include "binary_input_cases.hpp"
#include "binary_resource_cases.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/parameter_domains.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/record.hpp"
#include "execution_fixture.hpp"
#include "require.hpp"
#include "sauvola_reference.hpp"
#include "stub_verifier.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace docenhance::tests {
namespace {
UnusedVerifier verifier;
} // namespace
namespace {
constexpr std::size_t memory_limit = std::size_t{4} * 1024 * 1024;
constexpr std::uint8_t untouched = 42;
exec::Scheduler schedule(unsigned workers) {
    const auto concurrency = exec::Concurrency::resolve(workers, workers, 0, 0);
    REQUIRE(concurrency);
    return exec::Scheduler{*concurrency};
}
app::Invocation request(const std::string& selector) {
    app::Invocation value;
    value.command = app::Command::process;
    value.operands = {"input.png"};
    value.option(app::OptionId::out_dir) = "output";
    value.option(app::OptionId::output_mode) = "bw";
    value.option(app::OptionId::binarize) = selector;
    return value;
}
class SuccessfulProcessor final : public app::Processor {
  public:
    [[nodiscard]] unsigned calls() const noexcept {
        return calls_;
    }

    explicit SuccessfulProcessor(app::Published value) : value_(std::move(value)) {}
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const app::ExecutionControl& /*control*/) override {
        ++calls_;
        return value_;
    }

  private:
    unsigned calls_ = 0;
    app::Published value_;
};
void check_samples(image::PlaneView<const std::uint8_t> output, const SauvolaReference& reference) {
    for (std::uint32_t y = 0; y < output.height(); ++y) {
        for (std::uint32_t x = 0; x < output.width(); ++x) {
            require(output.row(y).subspan(x, 1).front() == (reference.at(x, y) == 0 ? 0 : 1),
                    "B02 reference sample");
        }
    }
}
// Keep the independent oracle in its original byte domain: even window4095 moments fit uint64.
// Production receives Q=257*byte; only the independent expected classification is normalized0/1.
void populate_sauvola_inputs(image::PlaneView<std::uint8_t> source,
                             image::PlaneView<std::uint16_t> codes) {
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            source.row(y).subspan(x, 1).front() =
                static_cast<std::uint8_t>(((x * 71U) + (y * 113U)) % 256U);
        }
    }
    for (std::uint32_t y = 0; y < source.height(); ++y) {
        for (std::uint32_t x = 0; x < source.width(); ++x) {
            codes.row(y)[x] = static_cast<std::uint16_t>(257U * source.row(y)[x]);
        }
    }
}
void check_sauvola_workers(const methods::Sauvola& method,
                           image::PlaneView<const std::uint16_t> codes,
                           image::PlaneView<std::uint8_t> output, core::Budget& budget,
                           const SauvolaReference& reference) {
    const auto held = budget.used();
    for (const unsigned workers : {1U, 2U, 8U}) {
        const auto scheduler = schedule(workers);
        REQUIRE(methods::binarize(method, {codes, output, budget, scheduler}));
        CHECK(budget.used() == held);
        check_samples(output.as_const(), reference);
    }
}
void compare(std::uint32_t width, std::uint32_t height, std::uint32_t window) {
    core::Budget budget{memory_limit};
    auto source = image::Plane<std::uint8_t>::allocate(budget, width, height);
    REQUIRE(source);
    auto codes = image::Plane<std::uint16_t>::allocate(budget, width, height);
    REQUIRE(codes);
    auto output = image::Plane<std::uint8_t>::allocate(budget, width, height);
    REQUIRE(output);
    populate_sauvola_inputs(source->view(), codes->view());
    const auto method = methods::Sauvola::create({.window = window});
    REQUIRE(method);
    const SauvolaReference reference{
        .source = source->view().as_const(),
        .window = window,
        .k = method->k(),
        .r = method->r(),
    };
    check_sauvola_workers(*method, codes->view().as_const(), output->view(), budget, reference);
}
void tiny_positive_sauvola_equality() {
    core::Budget budget{memory_limit};
    auto source = image::Plane<std::uint8_t>::allocate(budget, 1, 3);
    REQUIRE(source);
    auto codes = image::Plane<std::uint16_t>::allocate(budget, 1, 3);
    REQUIRE(codes);
    auto output = image::Plane<std::uint8_t>::allocate(budget, 1, 3);
    REQUIRE(output);
    std::ranges::fill(source->view().storage(), std::uint8_t{127});
    std::ranges::fill(output->view().storage(), untouched);
    const auto storage = source->view().storage();
    const std::vector<std::uint8_t> before(storage.begin(), storage.end());
    std::ranges::fill(codes->view().storage(), std::uint16_t{257 * 127});
    const auto entering_codes = codes->view().storage();
    const std::vector<std::uint16_t> codes_before(entering_codes.begin(), entering_codes.end());
    constexpr double k = 1e-20;
    const auto method = methods::Sauvola::create({.window = 4095, .k = k});
    require(method.has_value(), "actual tiny-positive k is admitted by the current factory");
    require(1.0 - k == 1.0, "binary64 subtraction rounds tiny-positive k away");
    const double independent_threshold = 127.0 * (1.0 + (k * -1.0));
    require(independent_threshold == 127.0, "independent constant threshold equals the sample");
    const auto held = budget.used();
    const auto scheduler = schedule(1);
    require(
        methods::binarize(*method, {codes->view().as_const(), output->view(), budget, scheduler})
            .has_value(),
        "actual admitted tiny-positive constant execution completes");
    require(budget.used() == held, "tiny-positive constant workspace fully refunds");
    require(std::ranges::equal(storage, before) && std::ranges::equal(entering_codes, codes_before),
            "original recipe and complete actual Q16 source and padding stay exact");
    const auto actual = output->view().storage();
    for (std::size_t i = 0; i < actual.size(); ++i) {
        require(actual[i] == (i % output->view().row_pitch() == 0 ? 0 : untouched),
                "all actual logical samples are black and destination padding remains sentinel");
    }
    const auto black = std::ranges::count(actual, std::uint8_t{0});
    require(black == 3, "all three actual singleton-column samples classified black");
    std::cout << "Sauvola tiny-positive k=" << k << " rounded_threshold=" << independent_threshold
              << " black_samples=" << black << " resident=" << held
              << " refunded=" << (budget.used() == held) << "\n";
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
    REQUIRE(
        methods::Sauvola::create({.window = 4095, .k = 0, .r = methods::sauvola_r_domain.minimum}));
    REQUIRE(methods::Sauvola::create({.window = 3, .k = 1, .r = 1}));
}
TEST_CASE("Application admits only options belonging to the selected method", "[app]") {
    auto value = request("sauvola");
    SuccessfulProcessor processor{observed_publication(value)};
    const auto admitted = app::prepare_process(value);
    REQUIRE(admitted);
    const auto& method =
        std::get<methods::Sauvola>(std::get<methods::BinaryOutput>(admitted->plan().output).method);
    CHECK(method.window() == 31);
    CHECK(method.k() == 0.2);
    CHECK(method.r() == 0.5);
    auto outcome = app::dispatch(value, processor, verifier);
    const auto* published = std::get_if<app::Published>(&outcome.payload);
    REQUIRE(published);
    REQUIRE(published->record.execution.binarization.has_value());
    if (!published->record.execution.binarization.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    CHECK(std::holds_alternative<methods::SauvolaFacts>(
        published->record.execution.binarization.value()));
    CHECK(processor.calls() == 1);
    value.option(app::OptionId::fixed_threshold) = "";
    CHECK(!app::prepare_process(value));
    value.option(app::OptionId::fixed_threshold) = "0.5";
    CHECK(!app::prepare_process(value));
    value = request("fixed");
    value.option(app::OptionId::sauvola_k) = "0.2";
    outcome = app::dispatch(value, processor, verifier);
    CHECK(std::holds_alternative<app::Failure>(outcome.payload));
    CHECK(processor.calls() == 1);
    value.option(app::OptionId::sauvola_k).reset();
    value.option(app::OptionId::sauvola_window) = "";
    CHECK(!app::prepare_process(value));
    value.option(app::OptionId::sauvola_window).reset();
    value.option(app::OptionId::sauvola_r) = "0.5";
    CHECK(!app::prepare_process(value));
}
TEST_CASE("Window grammar and explicitly empty numeric options are rejected", "[app]") {
    auto value = request("sauvola");
    for (const auto* const spelling :
         {"", "-3", "+3", "3.0", "3e0", " 3", "3 ", "0", "2", "4096", "4294967296"}) {
        value.option(app::OptionId::sauvola_window) = spelling;
        CHECK(!app::prepare_process(value));
    }
    value.option(app::OptionId::sauvola_window) = "0003";
    REQUIRE(app::prepare_process(value));
    value.option(app::OptionId::sauvola_k) = "";
    CHECK(!app::prepare_process(value));
    value.option(app::OptionId::sauvola_k).reset();
    value.option(app::OptionId::sauvola_r) = "";
    CHECK(!app::prepare_process(value));
    value = request("fixed");
    value.option(app::OptionId::fixed_threshold) = "";
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
    auto source = image::Plane<std::uint8_t>::allocate(budget, 1, 3);
    REQUIRE(source);
    auto codes = image::Plane<std::uint16_t>::allocate(budget, 1, 3);
    REQUIRE(codes);
    auto output = image::Plane<std::uint8_t>::allocate(budget, 1, 3);
    REQUIRE(output);
    const auto scheduler = schedule(8);
    for (const std::uint8_t sample : {std::uint8_t{0}, std::uint8_t{127}, std::uint8_t{255}}) {
        std::ranges::fill(source->view().storage(), sample);
        for (const double k : {0.0, 0.2, 1.0}) {
            const auto method = methods::Sauvola::create({.window = 4095, .k = k});
            REQUIRE(method);
            std::ranges::fill(codes->view().storage(), static_cast<std::uint16_t>(257U * sample));
            REQUIRE(methods::binarize(
                *method, {codes->view().as_const(), output->view(), budget, scheduler}));
            CHECK(output->view().row(0).front() == ((k == 0.0 || sample == 0) ? 0 : 1));
        }
    }
    const auto assertions_before = assertion_counter().load();
    tiny_positive_sauvola_equality();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Maximum-radius strip halos agree at every seam", "[sauvola]") {
    core::Budget budget{memory_limit};
    auto source = image::Plane<std::uint8_t>::allocate(budget, 8201, 1);
    REQUIRE(source);
    auto codes = image::Plane<std::uint16_t>::allocate(budget, 8201, 1);
    REQUIRE(codes);
    auto output = image::Plane<std::uint8_t>::allocate(budget, 8201, 1);
    REQUIRE(output);
    for (std::uint32_t x = 0; x < source->width(); ++x) {
        source->view().row(0).subspan(x, 1).front() = static_cast<std::uint8_t>((x * 71U) % 256U);
    }
    for (std::uint32_t x = 0; x < source->width(); ++x) {
        codes->view().row(0)[x] = static_cast<std::uint16_t>(257U * source->view().row(0)[x]);
    }
    const auto method = methods::Sauvola::create({.window = 4095});
    REQUIRE(method);
    for (const unsigned workers : {1U, 2U, 8U}) {
        const auto scheduler = schedule(workers);
        REQUIRE(methods::binarize(*method,
                                  {codes->view().as_const(), output->view(), budget, scheduler}));
        for (const auto x : {0U, 1U, 4095U, 4096U, 4097U, 8191U, 8192U, 8200U}) {
            require(output->view().row(0).subspan(x, 1).front() ==
                        singleton_row_reference(source->view().as_const(), x, *method),
                    "maximum-window seam reference");
        }
    }
}
TEST_CASE("Sauvola reserves exact bounded simultaneous scratch before writing", "[sauvola]") {
    core::Budget images{memory_limit};
    auto source = image::Plane<std::uint16_t>::allocate(images, 8201, 2);
    auto output = image::Plane<std::uint8_t>::allocate(images, 8201, 2);
    REQUIRE(source);
    REQUIRE(output);
    std::ranges::fill(source->view().storage(), std::uint16_t{257 * 127});
    std::ranges::fill(output->view().storage(), untouched);
    const auto original = source->view().storage();
    const std::vector<std::uint16_t> before(original.begin(), original.end());
    const auto method = methods::Sauvola::create();
    REQUIRE(method);
    const auto scheduler = schedule(8);
    const auto needed = binary_expected_scratch(*method, 8201, 2, scheduler.workers());
    core::Budget refused{needed - 1};
    const auto result =
        methods::binarize(*method, {source->view().as_const(), output->view(), refused, scheduler});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::resource);
    CHECK(refused.used() == 0);
    CHECK(std::ranges::all_of(output->view().storage(), [](auto p) { return p == untouched; }));
    core::Budget exact{needed};
    const auto completed =
        methods::binarize(*method, {source->view().as_const(), output->view(), exact, scheduler});
    REQUIRE(completed);
    CHECK(completed->black_samples + completed->white_samples == std::uint64_t{8201} * 2);
    CHECK(exact.peak() == needed);
    CHECK(exact.used() == 0);
    CHECK(std::ranges::equal(original, before));
    CHECK(!methods::binarize(*method, {{}, output->view(), exact, scheduler}));
    CHECK(!exec::Concurrency::resolve(0, 1, 0, 0));
    CHECK(!exec::Concurrency::resolve(257, 1, 0, 0));
    const auto maximum = methods::Sauvola::create({.window = 4095});
    REQUIRE(maximum);
    const auto width = static_cast<std::uint32_t>(image::source_pixels_max);
    const auto worst = binary_expected_scratch(*maximum, width, 1, 64);
    CHECK(worst <= (std::size_t{8} * 1024 * 1024) + binary_partial_bytes(width, 4096));
    // Counts are retained simultaneously with the <=8MiB two-moment slot allocation.
    const auto assertions_before = assertion_counter().load();
    binary_input_alias_cases();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("Otsu completed records retain the actual fitted method facts", "[app][otsu]") {
    const auto selected_request = request("otsu");
    const auto valid = observed_publication(selected_request, {.width = 1, .height = 1});
    REQUIRE(valid.record.execution.binarization.has_value());
    if (!valid.record.execution.binarization.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    const auto& actual = std::get<methods::OtsuFacts>(valid.record.execution.binarization.value());
    REQUIRE(actual.threshold_bin == 2047);
    REQUIRE(actual.single_bin_fallback);
    SuccessfulProcessor control{valid};
    const auto accepted = app::dispatch(selected_request, control, verifier);
    REQUIRE(accepted.exit_code() == core::ExitCode::success);
    const auto* published = std::get_if<app::Published>(&accepted.payload);
    REQUIRE(published);
    CHECK(published->record.execution.binarization == valid.record.execution.binarization);
    REQUIRE(record::Codec::encode(valid.record));
    const auto sauvola = observed_publication(request("sauvola"), {.width = 1, .height = 1});
    REQUIRE(record::Codec::encode(sauvola.record));
    for (unsigned change = 0; change < 4; ++change) {
        auto value = valid.record;
        if (!value.execution.binarization.has_value()) {
            throw std::logic_error("Required copied binarization observation is absent");
        }
        if (change == 0) {
            value.execution.binarization.reset();
        } else if (change == 1) {
            std::get<methods::OtsuFacts>(value.execution.binarization.value()).threshold_bin = 4095;
        } else if (change == 2) {
            std::get<methods::OtsuFacts>(value.execution.binarization.value()).threshold_bin = 2046;
        } else {
            value.request.binarization = sauvola.record.request.binarization;
        }
        CHECK(!record::Codec::encode(value));
    }
}
} // namespace docenhance::tests
