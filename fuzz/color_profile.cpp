// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/color/converter.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

#include <algorithm>
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
        source.shape = {.width = 1, .height = 1, .model = model, .depth = de::image::word_bits};
        source.pixels = de::image::Plane<std::uint8_t>::allocate(
                            budget, de::image::raster_row_bytes(source.shape).value(), 1)
                            .value();
        std::ranges::fill(source.pixels.view().row(0), 127);
        if (!profile.empty()) {
            source.metadata.icc = budget.allocate(profile.size()).value();
            std::memcpy(source.metadata.icc.bytes().data(), profile.data(), profile.size());
        }
        auto converter =
            de::color::Converter::create(source, de::image::Continuous::create({}).value(), budget);
        if (converter) {
            const auto shape = (*converter)->descriptor().shape;
            auto output = de::image::Plane<std::uint8_t>::allocate(
                budget, de::image::raster_row_bytes(shape).value(), 1);
            if (output) {
                const auto result =
                    (*converter)->row(0, output->view().row(0), de::image::RowUse::output);
                de::fuzz::require(
                    result.has_value() || result.error().code == de::core::ErrorCode::input ||
                        result.error().code == de::core::ErrorCode::resource,
                    "native ICC samples produce valid output or a named input/resource failure");
            }
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
