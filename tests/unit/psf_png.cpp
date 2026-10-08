// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/io/psf_png.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/linear.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/digest.hpp"
#include "png_fixture.hpp"
#include "source_snapshot.hpp"
#include "temporary_directory.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string_view>
#include <vector>
namespace docenhance::tests {
namespace {
constexpr std::size_t psf_codec_budget = std::size_t{8} * 1024 * 1024;
constexpr std::size_t png_header_end = 33;
std::vector<std::uint8_t> coefficient_rows(image::Extent extent, unsigned depth) {
    const auto width = extent.width;
    const auto height = extent.height;
    std::vector<std::uint8_t> raw;
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto low = static_cast<std::uint8_t>((x + (y * width)) & 0xffU);
            if (depth == 16) {
                raw.push_back(static_cast<std::uint8_t>(((x * 7) + y) & 0xffU));
            }
            raw.push_back(low);
        }
    }
    return raw;
}
std::vector<std::uint8_t> coefficient_png(std::uint32_t width = 3, std::uint32_t height = 3,
                                          unsigned depth = 8, unsigned color = 0) {
    std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<std::uint8_t> header;
    append_be32(header, width);
    append_be32(header, height);
    header.insert(header.end(),
                  {static_cast<std::uint8_t>(depth), static_cast<std::uint8_t>(color), 0, 0, 0});
    append_chunk(png, "IHDR", header);
    append_chunk(png, "IDAT",
                 stored_deflate(coefficient_rows({.width = width, .height = height}, depth)));
    append_chunk(png, "IEND", {});
    return png;
}
void insert_metadata(std::vector<std::uint8_t>& png, std::string_view name,
                     std::span<const std::uint8_t> data) {
    std::vector<std::uint8_t> chunk;
    append_chunk(chunk, name, data);
    png.insert(png.begin() + png_header_end, chunk.begin(), chunk.end());
}
std::vector<std::uint8_t> valid_gray_iccp() {
    core::Budget budget{psf_codec_budget};
    const auto raster = io::decode_psf_png(coefficient_png(), budget).value();
    const auto converter =
        color::Converter::create(raster, image::Continuous::create({}).value(), budget).value();
    const auto encoded = stored_deflate(converter->descriptor().profile);
    std::vector<std::uint8_t> declaration{'P', 'S', 'F', 0, 0};
    declaration.insert(declaration.end(), encoded.begin(), encoded.end());
    return declaration;
}
void write_psf(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream stream{path, std::ios::binary};
    for (const auto byte : bytes) {
        stream.put(static_cast<char>(byte));
    }
    stream.close();
    REQUIRE(stream);
}
void compare_coefficient_bytes(const image::Raster& raster, unsigned depth) {
    const auto raw =
        coefficient_rows({.width = raster.shape.width, .height = raster.shape.height}, depth);
    const auto stride = std::size_t{raster.shape.width} * (depth / 8);
    for (std::uint32_t y = 0; y < raster.shape.height; ++y) {
        const auto expected = std::span{raw}.subspan((std::size_t{y} * (stride + 1)) + 1, stride);
        CHECK(std::ranges::equal(raster.pixels.view().row(y), expected));
    }
    CHECK(raster.shape.depth.bits() == depth);
    CHECK(raster.shape.model == image::SampleModel::gray);
    CHECK(raster.metadata.orientation == image::Orientation::normal());
}
} // namespace
TEST_CASE("PSF PNG coefficients retain raw 8 and 16 bit network bytes and rectangular axes",
          "[io][restoration]") {
    for (const auto depth : {8U, 16U}) {
        for (const auto height : {3U, 5U}) {
            core::Budget budget{psf_codec_budget};
            {
                auto decoded = io::decode_psf_png(coefficient_png(3, height, depth), budget);
                REQUIRE(decoded);
                CHECK(decoded->shape.width == 3);
                CHECK(decoded->shape.height == height);
                compare_coefficient_bytes(*decoded, depth);
            }
            CHECK(budget.used() == 0);
        }
    }
}
TEST_CASE("PSF PNG ignores interpretation declarations without changing stored coefficients",
          "[io][restoration]") {
    const auto declarations = std::to_array<std::string_view>({"gAMA", "iCCP", "eXIf"});
    for (const auto declaration : declarations) {
        for (const auto& payload :
             {std::vector<std::uint8_t>{0}, std::vector<std::uint8_t>{0, 0, 177, 143}}) {
            auto bytes = coefficient_png(3, 5, 16);
            insert_metadata(bytes, declaration, payload);
            core::Budget budget{psf_codec_budget};
            auto decoded = io::decode_psf_png(bytes, budget);
            REQUIRE(decoded);
            compare_coefficient_bytes(*decoded, 16);
        }
    }
}
TEST_CASE("PSF PNG also ignores valid ICC and EXIF orientation declarations", "[io][restoration]") {
    const auto exif = std::to_array<std::uint8_t>({
        'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 1, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0,
    });
    auto bytes = coefficient_png(3, 5, 16);
    insert_metadata(bytes, "eXIf", exif);
    insert_metadata(bytes, "iCCP", valid_gray_iccp());
    core::Budget budget{psf_codec_budget};
    const auto result = io::decode_psf_png(bytes, budget);
    REQUIRE(result);
    CHECK(result->shape.width == 3);
    CHECK(result->shape.height == 5);
    compare_coefficient_bytes(*result, 16);
}
TEST_CASE("PSF PNG domain refusals happen before pixel allocation", "[io][restoration]") {
    for (const auto depth : {1U, 2U, 4U}) {
        core::Budget budget{0};
        const auto result = io::decode_psf_png(coefficient_png(3, 3, depth), budget);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::input);
        CHECK(budget.used() == 0);
    }
    for (const auto color : {2U, 3U, 4U, 6U}) {
        core::Budget budget{0};
        const auto result = io::decode_psf_png(coefficient_png(3, 3, 8, color), budget);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::input);
    }
    for (const auto width : {1U, 2U, 4U, 131U}) {
        core::Budget budget{0};
        const auto result = io::decode_psf_png(coefficient_png(width, 3), budget);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::input);
    }
    for (const auto height : {1U, 4U, 131U}) {
        core::Budget budget{0};
        const auto result = io::decode_psf_png(coefficient_png(3, height), budget);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::input);
    }
}
TEST_CASE("PSF PNG rejects transparency, bad framing, CRC and animation", "[io][restoration]") {
    for (unsigned change = 0; change < 5; ++change) {
        auto bytes = coefficient_png();
        if (change == 0) {
            insert_metadata(bytes, "tRNS", std::array<std::uint8_t, 2>{0, 255});
        } else if (change == 1) {
            bytes.push_back(0);
        } else if (change == 2) {
            bytes.back() ^= std::uint8_t{1};
        } else if (change == 3) {
            insert_metadata(bytes, "acTL", std::array<std::uint8_t, 8>{});
        } else {
            bytes.resize(bytes.size() - 1);
        }
        core::Budget budget{psf_codec_budget};
        const auto result = io::decode_psf_png(bytes, budget);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::input);
        CHECK(budget.used() == 0);
    }
}
TEST_CASE("PSF encoded and charged-buffer refusal never downsample or retain storage",
          "[io][restoration]") {
    const auto bytes = coefficient_png(io::psf_dimension_max, io::psf_dimension_max, 16);
    core::Budget refused{1};
    const auto refused_result = io::decode_psf_png(bytes, refused);
    REQUIRE(!refused_result);
    CHECK(refused_result.error().code == core::ErrorCode::resource);
    CHECK(refused.used() == 0);
    core::Budget budget{psf_codec_budget};
    {
        const auto decoded = io::decode_psf_png(bytes, budget);
        REQUIRE(decoded);
        CHECK(decoded->shape.width == io::psf_dimension_max);
        CHECK(decoded->shape.height == io::psf_dimension_max);
    }
    CHECK(budget.used() == 0);
    auto oversized = coefficient_png();
    insert_metadata(oversized, "tEXt", std::vector<std::uint8_t>(io::psf_encoded_bytes_max, 1));
    const auto encoded_result = io::decode_psf_png(oversized, budget);
    REQUIRE(!encoded_result);
    CHECK(encoded_result.error().code == core::ErrorCode::resource);
    CHECK(budget.used() == 0);
}
TEST_CASE("PSF snapshot retains Unicode identity and enforces its byte ceiling before reading",
          "[io][restoration]") {
    const TemporaryDirectory directory{"docenhance-psf"};
#ifdef _WIN32
    const auto path = directory.path / L"caf\u00e9.png";
#else
    const auto path = directory.path / "caf\xc3\xa9.png";
#endif
    const auto bytes = coefficient_png(3, 5, 16);
    write_psf(path, bytes);
    core::Budget budget{psf_codec_budget};
    {
        const auto loaded = io::load_psf_png(utf8_spelling(path), budget);
        REQUIRE(loaded);
        const auto expected = io::identify(std::as_bytes(std::span{bytes}));
        REQUIRE(expected);
        CHECK(loaded->source.sha256 == expected->sha256);
        CHECK(loaded->source.bytes == bytes.size());
        compare_coefficient_bytes(loaded->raster, 16);
    }
    CHECK(budget.used() == 0);
    const CheckpointStop cancelled{core::Checkpoint::decode, 0};
    for (const auto maximum :
         {std::size_t{0}, static_cast<std::size_t>(image::source_encoded_bytes_max) + 1}) {
        const auto result = io::read_source_snapshot(utf8_spelling(path), budget,
                                                     cancelled.cancellation(), maximum);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::argument);
    }
    const auto too_small =
        io::read_source_snapshot(utf8_spelling(path), budget, {}, bytes.size() - 1);
    REQUIRE(!too_small);
    CHECK(too_small.error().code == core::ErrorCode::resource);
    CHECK(budget.used() == 0);
}
TEST_CASE("PSF file size is refused before charging an encoded snapshot", "[io][restoration]") {
    const TemporaryDirectory directory{"docenhance-psf-ceiling"};
    const auto path = directory.path / "oversized.png";
    write_psf(path, std::vector<std::uint8_t>(io::psf_encoded_bytes_max + 1, 0));
    core::Budget budget{0};
    const auto refused = io::load_psf_png(utf8_spelling(path), budget);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::resource);
    CHECK(refused.error().message.contains("encoded input ceiling"));
    CHECK(budget.used() == 0);
}
TEST_CASE("Every PSF decode checkpoint cancels with a full refund", "[io][restoration]") {
    const auto bytes = coefficient_png(3, 5, 16);
    bool completed = false;
    constexpr std::size_t attempts = 256;
    for (std::size_t allowed = 0; allowed < attempts; ++allowed) {
        const CheckpointStop stop{core::Checkpoint::decode, allowed};
        core::Budget budget{psf_codec_budget};
        {
            const auto decoded = io::decode_psf_png(bytes, budget, stop.cancellation());
            if (decoded) {
                CHECK(!CheckpointStop::stopped());
                CHECK(allowed > 0);
                completed = true;
            } else {
                CHECK(CheckpointStop::stopped());
                CHECK(decoded.error().code == core::ErrorCode::cancelled);
            }
        }
        CHECK(budget.used() == 0);
        if (completed) {
            break;
        }
    }
    REQUIRE(completed);
}
} // namespace docenhance::tests
