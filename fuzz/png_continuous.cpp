// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/continuous_png.hpp"
#include "docenhance/color/interpretation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "png_depth_fixture.hpp"
#include "support/color_samples.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"
#include "support/png_shape.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
namespace {

void rows(docenhance::color::Interpretation& producer, docenhance::core::Budget& budget) {
    namespace de = docenhance;
    const auto descriptor = producer.descriptor();
    const auto shape = descriptor.output;
    const auto channels = producer.category() == de::image::InterpretationCategory::gray1 ? 1U : 3U;
    const auto capacity = std::min(shape.width, de::image::linear_block_pixels);
    auto storage = de::image::Plane<double>::allocate(budget, capacity * channels, 2);
    if (!storage) {
        de::fuzz::require(storage.error().code == de::core::ErrorCode::resource,
                          "linear comparison storage refuses only resources");
        return;
    }
    auto reader = producer.reader();
    de::fuzz::require(reader.has_value(), "unleased interpreter admits its owned reader");
    de::image::BlockObservation total;
    for (std::uint32_t row = 0; row < shape.height; ++row) {
        for (std::uint32_t first = 0; first < shape.width; first += capacity) {
            const auto count = std::min(capacity, shape.width - first) * channels;
            const auto read = [&](std::span<double> output) {
                return channels == 1 ? reader.value().read_gray(row, first, output.first(count))
                                     : reader.value().read_rgb(row, first, output.first(count));
            };
            const auto initial = read(storage->view().row(0));
            de::fuzz::require(producer.descriptor() == descriptor,
                              "extraction leaves interpretation metadata immutable");
            const auto repeated = read(storage->view().row(1));
            de::fuzz::require(initial.has_value() == repeated.has_value(),
                              "linear block outcome repeats");
            de::fuzz::require(producer.descriptor() == descriptor,
                              "repeated extraction cannot accumulate interpretation counts");
            if (!initial) {
                de::fuzz::require(initial.error().code == repeated.error().code &&
                                      (initial.error().code == de::core::ErrorCode::input ||
                                       initial.error().code == de::core::ErrorCode::resource),
                                  "color blocks repeat only input or resource refusals");
                return;
            }
            de::fuzz::require(*initial == *repeated, "owned block observations repeat exactly");
            de::fuzz::require(std::ranges::equal(storage->view().row(0).first(count),
                                                 storage->view().row(1).first(count)),
                              "continuous linear samples repeat exactly");
            de::fuzz::require(de::image::valid_unit_samples(storage->view().row(0).first(count)),
                              "interpreted output is finite unit luminance or RGB");
            total.flattened_pixels += initial->flattened_pixels;
            total.clipped_components += initial->clipped_components;
        }
    }
    const auto pixels = std::uint64_t{shape.width} * shape.height;
    de::fuzz::require(total.flattened_pixels <= pixels &&
                          total.clipped_components <= pixels * channels,
                      "extraction owner folds each block observation once");
}
void decode(std::span<const std::uint8_t> bytes, docenhance::image::ProfilePolicy policy,
            docenhance::core::Budget& budget) {
    auto source = docenhance::codec::decode_png_raster(bytes, budget, policy, {},
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
    const auto oriented =
        docenhance::image::oriented_shape(source->shape, source->metadata.orientation);
    const docenhance::image::PngShape output{
        .width = oriented.width,
        .height = oriented.height,
        .model = docenhance::image::is_color(source->shape.model)
                     ? docenhance::image::SampleModel::rgb
                     : docenhance::image::SampleModel::gray,
        .depth = docenhance::tests::fixture_png_depth(source->shape.depth.bits()),
    };
    auto interpreter = docenhance::color::Interpretation::create(
        *source, {.profile = policy, .rotation = {}}, output, budget);
    if (interpreter) {
        rows(**interpreter, budget);
    } else {
        docenhance::fuzz::require(interpreter.error().code == docenhance::core::ErrorCode::input ||
                                      interpreter.error().code ==
                                          docenhance::core::ErrorCode::resource,
                                  "color interpretation refuses only input or resource failures");
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
