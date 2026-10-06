// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/tiff.hpp"
#include "tiff_fixture.hpp"

#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
namespace docenhance::tests {
namespace {
constexpr std::size_t tiff_budget = std::size_t{64} * 1024 * 1024;
void exact_samples(const image::Raster& raster, unsigned depth, unsigned channels) {
    CHECK(raster.shape.depth.bits() == depth);
    CHECK(image::components(raster.shape.model) == channels);
    constexpr unsigned fixture_width = 19;
    constexpr unsigned fixture_height = 17;
    REQUIRE(raster.shape.width == fixture_width);
    REQUIRE(raster.shape.height == fixture_height);
    for (unsigned index = 0; index < fixture_width * fixture_height * channels; ++index) {
        const auto c = index % channels;
        const auto pixel = index / channels;
        const auto x = pixel % fixture_width;
        const auto y = pixel / fixture_width;
        const auto row = raster.pixels.view().row(y);
        const unsigned expected = depth == image::byte_bits
                                      ? ((19 * x) + (37 * y) + (53 * c)) % 256
                                      : ((1493 * x) + (2039 * y) + (8191 * c) + 1) % 65536;
        const auto actual = image::read_sample(
            row.subspan(((std::size_t{x} * channels) + c) * raster.shape.depth.bytes()),
            raster.shape.depth);
        REQUIRE(actual == expected);
    }
}
} // namespace
namespace {
void matrix_case(unsigned depth, unsigned channels, unsigned flags) {
    const auto little = flags % 2;
    const auto big = (flags / 2) % 2;
    constexpr unsigned planar_bit = 4;
    constexpr unsigned tiled_bit = 8;
    const auto planar = ((flags / planar_bit) % 2) + 1;
    const auto tiled = flags / tiled_bit;
    const auto name = "d" + std::to_string(depth) + "-c" + std::to_string(channels) + "-t" +
                      std::to_string(tiled) + "-p" + std::to_string(planar) + "-b" +
                      std::to_string(big) + "-l" + std::to_string(little) + ".tif";
    INFO(name);
    core::Budget budget{tiff_budget};
    {
        const auto decoded =
            io::decode_tiff(tiff_fixture(name), budget, image::ProfilePolicy::embedded);
        REQUIRE(decoded);
        CHECK(image::valid_tiff_source(decoded->source));
        exact_samples(decoded->raster, depth, channels);
    }
    CHECK(budget.used() == 0);
}
} // namespace
TEST_CASE("TIFF exact 8/16-bit samples cross endian, BigTIFF, planes and strile edges", "[tiff]") {
    constexpr unsigned layout_combinations = 16;
    for (const auto depth : {image::byte_bits, image::word_bits}) {
        for (const auto channels : {1U, 2U, image::tiff_color_samples, image::rgba_channels}) {
            for (unsigned flags = 0; flags < layout_combinations; ++flags) {
                matrix_case(depth, channels, flags);
            }
        }
    }
}
TEST_CASE("TIFF lossless codecs and predictors retain independent sample values", "[tiff]") {
    struct CodecCase {
        unsigned compression{};
        unsigned predictor{};
    };
    constexpr auto codecs = std::to_array<CodecCase>({
        {.compression = image::tiff_lzw, .predictor = 1},
        {.compression = image::tiff_lzw, .predictor = 2},
        {.compression = image::tiff_deflate, .predictor = 1},
        {.compression = image::tiff_deflate, .predictor = 2},
        {.compression = image::tiff_adobe_deflate, .predictor = 1},
        {.compression = image::tiff_adobe_deflate, .predictor = 2},
        {.compression = image::tiff_packbits, .predictor = 1},
    });
    for (const auto codec : codecs) {
        for (const auto depth : {image::byte_bits, image::word_bits}) {
            const auto name = "compression-" + std::to_string(codec.compression) + "-d" +
                              std::to_string(depth) + "-predictor" +
                              std::to_string(codec.predictor) + ".tif";
            INFO(name);
            core::Budget budget{tiff_budget};
            const auto decoded =
                io::decode_tiff(tiff_fixture(name), budget, image::ProfilePolicy::embedded);
            REQUIRE(decoded);
            exact_samples(decoded->raster, depth, image::tiff_color_samples);
        }
    }
}
TEST_CASE("TIFF CCITT decoding retains bilevel MINISWHITE sample meaning", "[tiff]") {
    for (const unsigned compression : {3U, 4U}) {
        core::Budget budget{tiff_budget};
        const auto decoded =
            io::decode_tiff(tiff_fixture("fax-" + std::to_string(compression) + ".tif"), budget,
                            image::ProfilePolicy::embedded);
        REQUIRE(decoded);
        CHECK(decoded->source.photometric == 0);
        const auto pixels = decoded->raster.pixels.view();
        for (unsigned y = 0; y < 4; ++y) {
            const auto row = pixels.row(y);
            for (const auto sample : row) {
                REQUIRE(sample == ((y % 2) == 0 ? 0 : image::byte_max));
            }
        }
    }
}
namespace {
int jpeg_reference(unsigned photo, unsigned channel, unsigned x, unsigned y) {
    if (photo == image::tiff_minisblack) {
        constexpr unsigned block_width = 8;
        constexpr unsigned dc_base = 48;
        constexpr unsigned horizontal_dc = 29;
        constexpr unsigned vertical_dc = 43;
        constexpr unsigned dc_range = 160;
        return static_cast<int>(
            dc_base +
            (((horizontal_dc * (x / block_width)) + (vertical_dc * (y / block_width))) % dc_range));
    }
    constexpr auto rgb = std::to_array<int>({180, 80, 30});
    constexpr auto ycc_rgb = std::to_array<int>({233, 134, 103});
    return (photo == image::tiff_rgb ? rgb : ycc_rgb).at(channel);
}
void jpeg_samples(const image::Raster& raster, unsigned photo) {
    const auto channels = image::components(raster.shape.model);
    const auto count = raster.shape.width * raster.shape.height * channels;
    for (unsigned index = 0; index < count; ++index) {
        const auto channel = index % channels;
        const auto pixel = index / channels;
        const auto x = pixel % raster.shape.width;
        const auto y = pixel / raster.shape.width;
        const auto actual =
            raster.pixels.view().row(y).subspan((std::size_t{x} * channels) + channel, 1).front();
        const auto expected = jpeg_reference(photo, channel, x, y);
        CHECK(std::abs(static_cast<int>(actual) - expected) <= 1);
    }
}
void jpeg_case(unsigned photo, const std::string& suffix) {
    core::Budget budget{tiff_budget};
    {
        const auto decoded =
            io::decode_tiff(tiff_fixture("jpeg-" + std::to_string(photo) + suffix + ".tif"), budget,
                            image::ProfilePolicy::embedded);
        REQUIRE(decoded);
        jpeg_samples(decoded->raster, photo);
    }
    CHECK(budget.used() == 0);
}
} // namespace
TEST_CASE("TIFF nested JPEG uses charged allocation and one YCbCr interpretation", "[tiff]") {
    for (const auto* const suffix : {"", "-progressive"}) {
        for (const auto photo : {image::tiff_minisblack, image::tiff_rgb, image::tiff_ycbcr}) {
            jpeg_case(photo, suffix);
        }
    }
}
TEST_CASE("TIFF JPEG progress bounds valid repeated scans before publication", "[tiff]") {
    core::Budget budget{tiff_budget};
    {
        const auto complete = io::decode_tiff(tiff_fixture("jpeg-bounded-scans.tif"), budget,
                                              image::ProfilePolicy::embedded);
        REQUIRE(complete);
        jpeg_samples(complete->raster, image::tiff_minisblack);
    }
    CHECK(budget.used() == 0);
    const auto refused = io::decode_tiff(tiff_fixture("jpeg-many-scans.tif"), budget,
                                         image::ProfilePolicy::embedded);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::resource);
    CHECK(budget.used() == 0);
}
TEST_CASE("TIFF Deflate output bytes do not substitute for a completed checksum trailer",
          "[tiff]") {
    core::Budget budget{tiff_budget};
    CHECK(!io::decode_tiff(tiff_fixture("deflate-missing-checksum.tif"), budget,
                           image::ProfilePolicy::embedded));
    CHECK(budget.used() == 0);
}
TEST_CASE("TIFF native decode truncations, charged refusals and cancellation refund ownership",
          "[tiff]") {
    const auto fixture = tiff_fixture("jpeg-6.tif");
    for (std::size_t extent = 0; extent < fixture.size(); ++extent) {
        core::Budget budget{tiff_budget};
        CHECK(!io::decode_tiff(std::span{fixture}.first(extent), budget,
                               image::ProfilePolicy::embedded));
        CHECK(budget.used() == 0);
    }
    const auto reservation = io::tiff_native_payload_max + io::tiff_control_reservation;
    bool success = false;
    bool native_refused = false;
    for (std::size_t extra = 0; extra < std::size_t{256} * 1024; extra += 1024) {
        core::Budget budget{reservation + extra};
        {
            const auto decoded = io::decode_tiff(fixture, budget, image::ProfilePolicy::embedded);
            if (decoded) {
                success = true;
            } else {
                CHECK(decoded.error().code == core::ErrorCode::resource);
                native_refused = native_refused || extra > 65536;
            }
        }
        CHECK(budget.used() == 0);
    }
    CHECK(native_refused);
    CHECK(success);
    bool completed = false;
    for (std::size_t allowed = 0; allowed < 10000; ++allowed) {
        core::Budget budget{tiff_budget};
        const CheckpointStop stop{core::Checkpoint::decode, allowed};
        {
            const auto decoded = io::decode_tiff(fixture, budget, image::ProfilePolicy::embedded,
                                                 stop.cancellation());
            if (decoded) {
                completed = true;
            } else {
                REQUIRE(stop.stopped());
                CHECK(decoded.error().code == core::ErrorCode::cancelled);
            }
        }
        CHECK(budget.used() == 0);
        if (completed) {
            break;
        }
    }
    CHECK(completed);
}
} // namespace docenhance::tests
