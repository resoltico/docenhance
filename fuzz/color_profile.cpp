// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/color/interpretation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "png_depth_fixture.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
namespace {
namespace de = docenhance;
void attempt(std::span<const std::uint8_t> profile, docenhance::image::SampleModel model) {
    constexpr std::size_t memory_limit = std::size_t{8} * 1024 * 1024;
    de::core::Budget budget{memory_limit};
    {
        de::image::Raster source;
        source.shape = {
            .width = 1,
            .height = 1,
            .model = model,
            .depth = de::image::SampleDepth::word(),
        };
        source.pixels = de::image::Plane<std::uint8_t>::allocate(
                            budget, de::image::raster_row_bytes(source.shape).value(), 1)
                            .value();
        std::ranges::fill(source.pixels.view().row(0), std::uint8_t{127});
        if (!profile.empty()) {
            source.metadata.icc = budget.allocate(profile.size()).value();
            std::memcpy(source.metadata.icc.bytes().data(), profile.data(), profile.size());
        }
        const de::image::PngShape shape{
            .width = 1,
            .height = 1,
            .model = model,
            .depth = docenhance::tests::fixture_png_depth(16),
        };
        const de::image::InterpretationPolicy policy{};
        auto interpreter = de::color::Interpretation::create(source, policy, shape, budget);
        if (interpreter) {
            const auto descriptor = (*interpreter)->descriptor();
            de::fuzz::require(descriptor.output == shape, "ICC output shape remains admitted");
            auto reader = interpreter.value()->reader();
            de::fuzz::require(reader.has_value(), "ICC interpreter admits its exclusive reader");
            std::array<double, 3> first{};
            std::array<double, 3> second{};
            const auto read = [&](std::span<double> output) {
                return model == de::image::SampleModel::gray
                           ? reader.value().read_gray(0, 0, output.first(1))
                           : reader.value().read_rgb(0, 0, output);
            };
            const auto initial = read(first);
            de::fuzz::require((*interpreter)->descriptor() == descriptor,
                              "ICC read returns observations without mutating its descriptor");
            const auto repeated = read(second);
            de::fuzz::require(initial.has_value() == repeated.has_value(),
                              "ICC sample outcome repeats");
            if (initial) {
                de::fuzz::require(de::image::valid_unit_samples(first),
                                  "ICC interpretation returns finite unit samples");
                de::fuzz::require(*initial == *repeated && first == second,
                                  "ICC linear samples and owned observations repeat exactly");
            } else {
                de::fuzz::require(initial.error().code == repeated.error().code &&
                                      (initial.error().code == de::core::ErrorCode::input ||
                                       initial.error().code == de::core::ErrorCode::resource),
                                  "native ICC samples repeat named input/resource failure");
            }
            de::fuzz::require((*interpreter)->descriptor() == descriptor,
                              "repeated ICC extraction cannot accumulate observations");
        } else {
            de::fuzz::require(interpreter.error().code == de::core::ErrorCode::input ||
                                  interpreter.error().code == de::core::ErrorCode::resource,
                              "raw ICC refuses only input or resource failures");
        }
        de::fuzz::require(budget.used() <= memory_limit,
                          "native color allocations respect their budget");
    }
    de::fuzz::require(budget.used() == 0,
                      "malformed profile and transform paths refund every allocation");
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    attempt({data, size}, docenhance::image::SampleModel::gray);
    attempt({data, size}, docenhance::image::SampleModel::rgb);
    return 0;
}
