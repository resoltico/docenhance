// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/fft/fft.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/concurrency.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/plane.hpp"
#include "fft_cases.hpp"
#include "require.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>
namespace docenhance::tests {
namespace {
std::span<double> fft_scalar_backing(fft::Spectrum& spectrum) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* const scalars = reinterpret_cast<double*>(spectrum.storage().data());
    return {scalars, 2 * spectrum.storage().size()};
}
void inverse_alias(fft::Spectrum& spectrum, core::Budget& budget, std::size_t offset) {
    const auto scalars = fft_scalar_backing(spectrum);
    require(spectrum.shape().width == 1 && spectrum.storage().size() >= 2 && scalars.size() >= 4,
            "live singleton spectrum has one active Complex and constructed row padding");
    const auto alias = image::PlaneView<double>::create(
        scalars.subspan(offset, 1), {.width = 1, .height = 1, .stride = sizeof(double)});
    require(alias && alias->storage().size() == 1 && alias->width() == 1 && alias->height() == 1 &&
                alias->shape().stride == sizeof(double),
            "each aliased real view has independently valid singleton geometry and backing");
    require(std::as_bytes(alias->storage()).data() ==
                    std::as_bytes(spectrum.storage())
                        .subspan(offset * sizeof(double), sizeof(double))
                        .data() &&
                (offset == 0 || offset * sizeof(double) >= spectrum.row(0).size_bytes()),
            "direct alias starts in coefficient; offset two's entire backing is in live padding");
    const auto before = fft_bits<fft::Complex>(spectrum.storage());
    const auto held = budget.used();
    const CheckpointStop stop{core::Checkpoint::allocation, 0};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                    stop.cancellation()};
    const auto result = fft::inverse(spectrum, *alias, budget, scheduler);
    require(!result && result.error().code == core::ErrorCode::argument &&
                CheckpointStop::visits() == 0 && budget.used() == held &&
                fft_bits<fft::Complex>(spectrum.storage()) == before,
            "public inverse rejects actual full-backing alias before pending allocation stop");
}
} // namespace
TEST_CASE("FFT public blocks match direct Fourier phase and inverse scale at actual real strides") {
    const auto assertions_before = assertion_counter().load();
    constexpr std::array shapes{
        image::Extent{.width = 5, .height = 4},   image::Extent{.width = 6, .height = 5},
        image::Extent{.width = 1, .height = 8},   image::Extent{.width = 8, .height = 1},
        image::Extent{.width = 4, .height = 125},
    };
    for (const auto shape : shapes) {
        fft_transform_case(shape.width, shape.height, false);
        fft_transform_case(shape.width, shape.height, true);
    }
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("FFT inverse independently authored DC and sine spectrum has actual phase and scale") {
    for (const auto shape : std::array{
             image::Extent{.width = 8, .height = 1},
             image::Extent{.width = 1, .height = 8},
             image::Extent{.width = 5, .height = 4},
         }) {
        FftReal carrier{shape.width, shape.height, 2, false};
        carrier.fill();
        FftReal expected{shape.width, shape.height, 4, false};
        FftReal output{shape.width, shape.height, 4, false};
        core::Budget budget{fft_test_budget};
        const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
        auto spectrum = fft::forward(carrier.view().as_const(), budget, scheduler);
        REQUIRE(spectrum);
        // The forward call supplies a legitimate owner only; discard EVERY produced coefficient.
        std::ranges::fill(spectrum->storage(), fft::Complex{});
        const auto count = static_cast<double>(shape.width) * shape.height;
        spectrum->row(0).front() = {count * .5, 0};
        if (shape.width > 1) {
            spectrum->row(0).subspan(1, 1).front() = {0, -count * .125};
        }
        if (shape.height > 1) {
            spectrum->row(1).front() = {0, -count * .0625};
            spectrum->row(shape.height - 1).front() = {0, count * .0625};
        }
        for (std::uint32_t y = 0; y < shape.height; ++y) {
            for (std::uint32_t x = 0; x < shape.width; ++x) {
                expected.view().row(y).subspan(x, 1).front() =
                    .5 + (.25 * std::sin(2 * std::numbers::pi * x / shape.width)) +
                    (.125 * std::sin(2 * std::numbers::pi * y / shape.height));
            }
        }
        const auto before = fft_bits<fft::Complex>(spectrum->storage());
        const auto held = budget.used();
        REQUIRE(fft::inverse(*spectrum, output.view(), budget, scheduler));
        fft_check_back(expected, output);
        CHECK(fft_bits<fft::Complex>(spectrum->storage()) == before);
        CHECK(budget.used() == held);
    }
}
TEST_CASE("FFT Spectrum moves retain actual ledger lifetimes and reject moved inputs") {
    const auto assertions_before = assertion_counter().load();
    FftReal input{5, 4, 2, true};
    input.fill();
    FftReal output{5, 4, 4, true};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    std::optional<fft::Spectrum> retained;
    {
        core::Budget budget{fft_test_budget};
        auto produced = fft::forward(input.view().as_const(), budget, scheduler);
        require(produced.has_value(), "real spectrum is produced before move/lifetime controls");
        retained.emplace(std::move(*produced));
        require(produced->empty() && produced->shape().width == 0 &&
                    produced->extent().height == 0 && produced->storage().empty(),
                "actual spectrum move clears source readiness and geometry");
        const auto refused = fft::inverse(*produced, output.view(), budget, scheduler);
        require(!refused && refused.error().code == core::ErrorCode::argument,
                "moved-from spectrum refuses before native work");
        require(budget.used() == retained->size_bytes(), "moved owner retains original charge");
    }
    core::Budget work{fft_test_budget};
    require(fft::inverse(*retained, output.view(), work, scheduler).has_value(),
            "actual spectrum owns ledger and payload after original Budget destruction");
    fft_check_back(input, output);
    require(work.used() == 0, "inverse temporaries refund independently of old input ledger");
    retained.reset();
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("FFT geometry budget and deterministic phase refusals release real owners") {
    const auto assertions_before = assertion_counter().load();
    FftReal invalid{7, 4, 2, true};
    invalid.fill();
    const exec::Scheduler healthy{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    core::Budget budget{fft_test_budget};
    const auto unsupported = fft::forward(invalid.view().as_const(), budget, healthy);
    require(!unsupported && unsupported.error().code == core::ErrorCode::argument &&
                budget.used() == 0,
            "unsupported prime axis refuses before actual allocation and has no Bluestein promise");
    FftReal input{5, 4, 2, true};
    input.fill();
    const auto before = fft_bits<double>(input.values);
    for (const auto phase : {
             core::Checkpoint::allocation,
             core::Checkpoint::initialization,
             core::Checkpoint::scheduling,
             core::Checkpoint::solving,
             core::Checkpoint::processing,
         }) {
        const CheckpointStop stop{phase, 0};
        const exec::Scheduler stopped{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                      stop.cancellation()};
        const auto refused = fft::forward(input.view().as_const(), budget, stopped);
        require(
            !refused && refused.error().code == core::ErrorCode::cancelled && budget.used() == 0 &&
                CheckpointStop::visits() == 1 && fft_bits<double>(input.values) == before,
            "attained real FFT phase stop returns no spectrum and refunds while preserving input");
    }
    core::Budget too_short{255};
    const auto refused = fft::forward(input.view().as_const(), too_short, healthy);
    require(!refused && refused.error().code == core::ErrorCode::resource && too_short.used() == 0,
            "actual256-byte spectrum allocation refuses with255 available");
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("FFT fixed row and column tasks retain exact output across one two seven workers") {
    const auto assertions_before = assertion_counter().load();
    constexpr std::uint32_t width = 768;
    constexpr std::uint32_t height = 480;
    std::vector<std::byte> spectrum_bits;
    std::vector<std::byte> output_bits;
    FftReal input{width, height, 2, true};
    input.fill();
    for (const auto workers : {1U, 2U, 7U}) {
        core::Budget budget{fft_test_budget};
        const exec::Scheduler scheduler{exec::Concurrency::resolve(workers, 7, 0, 0).value()};
        FftReal output{width, height, 4, true};
        const auto result = fft::forward(input.view().as_const(), budget, scheduler);
        require(result.has_value() && scheduler.workers() == workers,
                "actual8row/7column tasks use resolved worker setting");
        require(fft::inverse(*result, output.view(), budget, scheduler).has_value(),
                "actual inverse completes fixed tasks");
        fft_check_back(input, output);
        const auto current_spectrum = fft_bits<fft::Complex>(result->storage());
        const auto current_output = fft_bits<double>(output.values);
        if (workers == 1) {
            spectrum_bits = current_spectrum;
            output_bits = current_output;
        } else {
            require(
                spectrum_bits == current_spectrum && output_bits == current_output,
                "real1/2/7 fixed blocks give identical complete spectrum and output backing bits");
        }
    }
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("FFT inverse rejects mismatched geometry and nonfinite const input before native work") {
    const auto assertions_before = assertion_counter().load();
    FftReal input{5, 4, 2, true};
    input.fill();
    core::Budget budget{fft_test_budget};
    const exec::Scheduler healthy{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    auto spectrum = fft::forward(input.view().as_const(), budget, healthy).value();
    const auto held = budget.used();
    FftReal mismatch{5, 5, 4, true};
    const auto mismatch_before = fft_bits<double>(mismatch.values);
    const auto refused_shape = fft::inverse(spectrum, mismatch.view(), budget, healthy);
    require(!refused_shape && refused_shape.error().code == core::ErrorCode::argument &&
                budget.used() == held && fft_bits<double>(mismatch.values) == mismatch_before,
            "actual inverse shape admission refuses without stores or retained work");
    const auto refused_empty = fft::inverse(spectrum, image::PlaneView<double>{}, budget, healthy);
    require(!refused_empty && refused_empty.error().code == core::ErrorCode::argument &&
                budget.used() == held,
            "actual inverse refuses empty real backing before native work");
    spectrum.row(0).front() = {std::numeric_limits<double>::quiet_NaN(), 0};
    const auto poisoned = fft_bits<fft::Complex>(spectrum.storage());
    FftReal output{5, 4, 4, true};
    const auto output_before = fft_bits<double>(output.values);
    const CheckpointStop stop{core::Checkpoint::solving, 0};
    const exec::Scheduler stopped{exec::Concurrency::resolve(1, 1, 0, 0).value(),
                                  stop.cancellation()};
    const auto refused_value = fft::inverse(spectrum, output.view(), budget, stopped);
    require(!refused_value && refused_value.error().code == core::ErrorCode::numerical &&
                CheckpointStop::visits() == 0 && budget.used() == held &&
                fft_bits<fft::Complex>(spectrum.storage()) == poisoned &&
                fft_bits<double>(output.values) == output_before,
            "actual const copy rejects nonfinite spectrum before solve and refunds work exactly");
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("FFT inverse rejects direct and padding-only backing aliases before pending stop") {
    const auto assertions_before = assertion_counter().load();
    FftReal input{1, 1, 2, true};
    input.fill();
    FftReal output{1, 1, 2, true};
    core::Budget budget{fft_test_budget};
    const exec::Scheduler scheduler{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    auto spectrum = fft::forward(input.view().as_const(), budget, scheduler).value();
    const auto before = fft_bits<fft::Complex>(spectrum.storage());
    const auto held = budget.used();
    const auto healthy = fft::inverse(spectrum, output.view(), budget, scheduler);
    require(healthy && std::isfinite(output.view().row(0).front()) &&
                std::abs(output.view().row(0).front() - input.view().row(0).front()) < 1e-12 &&
                budget.used() == held && fft_bits<fft::Complex>(spectrum.storage()) == before,
            "same-geometry healthy disjoint inverse succeeds and preserves the complete input");
    inverse_alias(spectrum, budget, 0);
    inverse_alias(spectrum, budget, 2);
    REQUIRE(assertion_counter().load() > assertions_before);
}
TEST_CASE("FFT phase worker bounds shrink to the available reservation without changing spectra") {
    const image::Extent extent{.width = 128, .height = 128};
    FftReal input{extent.width, extent.height, 2, false};
    input.fill();
    const auto source_before = fft_bits<double>(input.values);
    const exec::Scheduler serial{exec::Concurrency::resolve(1, 1, 0, 0).value()};
    const exec::Scheduler requested{exec::Concurrency::resolve(2, 2, 0, 0).value()};
    core::Budget reference_budget{fft_test_budget};
    const auto reference = fft::forward(input.view().as_const(), reference_budget, serial);
    REQUIRE(reference);
    const auto expected = fft_bits<fft::Complex>(reference.value().storage());
    const auto spectrum_bytes = reference.value().size_bytes();
    const auto one = fft::workspace_bytes(extent, 1).value();
    const auto two = fft::workspace_bytes(extent, 2).value();
    REQUIRE(one > 0);
    REQUIRE(two > one);
    for (const auto workspace : {one - 1, one, two}) {
        core::Budget budget{spectrum_bytes + workspace};
        {
            const auto result = fft::forward(input.view().as_const(), budget, requested);
            if (workspace < one) {
                REQUIRE(!result);
                CHECK(result.error().code == core::ErrorCode::resource);
                CHECK(budget.used() == 0);
            } else {
                REQUIRE(result);
                CHECK(fft_bits<fft::Complex>(result.value().storage()) == expected);
                CHECK(budget.used() == spectrum_bytes);
                CHECK(budget.peak() == spectrum_bytes + workspace);
            }
            CHECK(fft_bits<double>(input.values) == source_before);
        }
        CHECK(budget.used() == 0);
    }
}
} // namespace docenhance::tests
