// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/color/converter.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "support/color_samples.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"
#include "support/png_shape.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
namespace {

void rows(docenhance::image::RowSource& producer, docenhance::core::Budget& budget) {
    const auto shape = producer.descriptor().shape;
    const auto size = docenhance::image::raster_row_bytes(shape).value();
    auto storage = docenhance::image::Plane<std::uint8_t>::allocate(budget, size, 2);
    if (!storage) {
        return;
    }
    for (std::uint32_t row = 0; row < shape.height; ++row) {
        const auto first =
            producer.row(row, storage->view().row(0), docenhance::image::RowUse::verification);
        const auto second =
            producer.row(row, storage->view().row(1), docenhance::image::RowUse::verification);
        docenhance::fuzz::require(first.has_value() == second.has_value(), "row outcome repeats");
        if (!first) {
            docenhance::fuzz::require(first.error().code == docenhance::core::ErrorCode::input ||
                                          first.error().code ==
                                              docenhance::core::ErrorCode::resource,
                                      "color rows refuse only input or resource failures");
        }
        if (first) {
            docenhance::fuzz::require(
                std::ranges::equal(storage->view().row(0), storage->view().row(1)),
                "continuous integer rows repeat exactly");
        }
    }
}
void decode(std::span<const std::uint8_t> bytes, docenhance::image::ProfilePolicy policy,
            docenhance::core::Budget& budget) {
    auto source = docenhance::io::decode_png_raster(bytes, budget, policy, {},
                                                    {.encoded_bytes = 65536, .pixels = 4096});
    if (docenhance::fuzz::zero_png_dimension(bytes)) {
        docenhance::fuzz::require(!source, "zero PNG dimensions cannot decode successfully");
    }
    if (!source) {
        if (docenhance::fuzz::zero_png_dimension(bytes)) {
            docenhance::fuzz::require(source.error().code == docenhance::core::ErrorCode::input,
                                      "zero PNG dimensions are malformed input, not resources");
        }
        docenhance::fuzz::require(source.error().code == docenhance::core::ErrorCode::input ||
                                      source.error().code == docenhance::core::ErrorCode::resource,
                                  "malformed PNG fails without an invariant error");
        return;
    }
    docenhance::fuzz::require(std::uint64_t{source->shape.width} * source->shape.height <= 4096,
                              "continuous PNG respects the tightened pixel ceiling");
    const auto operation = docenhance::image::Continuous::create({.profile = policy}).value();
    auto converter = docenhance::color::Converter::create(*source, operation, budget);
    if (converter) {
        rows(**converter, budget);
    } else {
        docenhance::fuzz::require(converter.error().code == docenhance::core::ErrorCode::input ||
                                      converter.error().code ==
                                          docenhance::core::ErrorCode::resource,
                                  "color conversion refuses only input or resource failures");
    }
}
void attempt(std::span<const std::uint8_t> bytes, docenhance::image::ProfilePolicy policy) {
    constexpr std::size_t limit = std::size_t{16} * 1024 * 1024;
    docenhance::core::Budget budget{limit};
    decode(bytes, policy, budget);
    docenhance::fuzz::require(budget.used() == 0,
                              "continuous decoding and color release every allocation");
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    docenhance::fuzz::check_color_samples({data, size});
    attempt({data, size}, docenhance::image::ProfilePolicy::embedded);
    attempt({data, size}, docenhance::image::ProfilePolicy::srgb);
    return 0;
}
