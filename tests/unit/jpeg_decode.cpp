// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "cancellation_probe.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/jpeg.hpp"
#include "jpeg_fixture.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <string>
namespace docenhance::tests {
namespace {
constexpr std::size_t byte_budget = std::size_t{32} * 1024 * 1024;
int gray_reference(std::uint32_t x, std::uint32_t y, bool ac) {
    const int dc = 48 + static_cast<int>((((x / 8) * 29) + ((y / 8) * 43)) % 160);
    if (!ac) {
        return dc;
    }
    constexpr double pi = std::numbers::pi;
    const double horizontal = 8.0 * std::cos(((2.0 * (x % 8)) + 1.0) * pi / 16.0);
    const double vertical = -5.0 * std::cos(((2.0 * (y % 8)) + 1.0) * pi / 16.0);
    return static_cast<int>(
        std::floor(dc + ((horizontal + vertical) / (4.0 * std::numbers::sqrt2)) + 0.5));
}
void check_gray_row(std::span<const std::uint8_t> row, std::uint32_t y, bool ac) {
    for (std::uint32_t x = 0; x < row.size(); ++x) {
        CHECK(std::abs(static_cast<int>(row[x]) - gray_reference(x, y, ac)) <= (ac ? 1 : 0));
    }
}
void check_gray_samples(const image::Raster& raster, bool ac) {
    const auto pixels = raster.pixels.view();
    for (std::uint32_t y = 0; y < pixels.height(); ++y) {
        check_gray_row(pixels.row(y), y, ac);
    }
}
} // namespace
TEST_CASE("JPEG coefficient fixtures decode with the specified integer sample meaning", "[jpeg]") {
    for (const auto* const process : {"baseline", "progressive"}) {
        for (const bool ac : {false, true}) {
            core::Budget budget{byte_budget};
            {
                const auto fixture =
                    jpeg_fixture(std::string(ac ? "gray-ac-" : "gray-1x1-") + process + ".jpg");
                auto decoded = io::decode_jpeg(fixture, budget, image::ProfilePolicy::embedded);
                REQUIRE(decoded);
                REQUIRE(decoded->raster.shape.width == 17);
                REQUIRE(decoded->raster.shape.height == 9);
                CHECK(decoded->raster.metadata.png() == nullptr);
                CHECK(decoded->source.color == image::JpegColor::gray);
                check_gray_samples(decoded->raster, ac);
                CHECK(decoded->decoder_charge_peak >= 65536);
            }
            CHECK(budget.used() == 0);
        }
    }
}
TEST_CASE("JPEG baseline and progressive color decode identically at partial MCU edges", "[jpeg]") {
    for (const auto* const layout :
         {"rgb-1x1", "ycbcr-1x1", "ycbcr-2x1", "ycbcr-2x2", "ycbcr-1x2", "ycbcr-4x1"}) {
        core::Budget budget{byte_budget};
        auto baseline = io::decode_jpeg(jpeg_fixture(std::string(layout) + "-baseline.jpg"), budget,
                                        image::ProfilePolicy::embedded);
        auto progressive = io::decode_jpeg(jpeg_fixture(std::string(layout) + "-progressive.jpg"),
                                           budget, image::ProfilePolicy::embedded);
        REQUIRE(baseline);
        REQUIRE(progressive);
        CHECK(baseline->raster.shape.model == image::SampleModel::rgb);
        for (std::uint32_t y = 0; y < 9; ++y) {
            CHECK(std::ranges::equal(baseline->raster.pixels.view().row(y),
                                     progressive->raster.pixels.view().row(y)));
        }
        const auto row = baseline->raster.pixels.view().row(0);
        if (baseline->source.color == image::JpegColor::rgb) {
            CHECK(row[0] == 180);
            CHECK(row[1] == 80);
            CHECK(row[2] == 30);
        } else {
            CHECK(std::abs(static_cast<int>(row[0]) - 233) <= 1);
            CHECK(std::abs(static_cast<int>(row[1]) - 134) <= 1);
            CHECK(std::abs(static_cast<int>(row[2]) - 103) <= 1);
        }
    }
}
TEST_CASE("JPEG native failures and resource refusals refund every charged allocation", "[jpeg]") {
    const auto fixture = jpeg_fixture("gray-ac-progressive.jpg");
    bool native_refused = false;
    bool succeeded = false;
    for (std::size_t limit = 0; limit <= 150000; limit += 1024) {
        core::Budget budget{limit};
        {
            const auto decoded = io::decode_jpeg(fixture, budget, image::ProfilePolicy::embedded);
            if (decoded) {
                succeeded = true;
            } else {
                CHECK(decoded.error().code == core::ErrorCode::resource);
                native_refused = native_refused || limit > 65536;
            }
        }
        CHECK(budget.used() == 0);
    }
    CHECK(native_refused);
    CHECK(succeeded);
    core::Budget budget{byte_budget};
    CHECK(!io::decode_jpeg(fixture, budget, image::ProfilePolicy::embedded, {}, {.scans = 1}));
    CHECK(budget.used() == 0);
    for (std::size_t extent = 0; extent < fixture.size(); ++extent) {
        CHECK(!io::decode_jpeg(std::span{fixture}.first(extent), budget,
                               image::ProfilePolicy::embedded));
        CHECK(budget.used() == 0);
    }
}
TEST_CASE("A complete JPEG frame cannot hide a native entropy recovery warning", "[jpeg]") {
    const auto encoded = jpeg_fixture("gray-ac-progressive.jpg");
    std::size_t last_scan = 0;
    for (std::size_t index = 0; index + 1 < encoded.size(); ++index) {
        if (encoded.at(index) == 255 && encoded.at(index + 1) == 218) {
            last_scan = index;
        }
    }
    REQUIRE(last_scan != 0);
    const std::size_t length =
        (std::size_t{encoded.at(last_scan + 2)} * 256) + encoded.at(last_scan + 3);
    auto broken = encoded;
    broken.resize(last_scan + 2 + length);
    broken.insert(broken.end(), {0, 255, 217});
    core::Budget budget{byte_budget};
    {
        const auto result = io::decode_jpeg(broken, budget, image::ProfilePolicy::embedded);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::input);
    }
    CHECK(budget.used() == 0);
}
TEST_CASE("JPEG cancellation reaches scan parsing, native allocation and actual decoding",
          "[jpeg]") {
    const auto fixture = jpeg_fixture("gray-ac-progressive.jpg");
    bool reached_native = false;
    for (std::size_t allowed = 0; allowed < 200; ++allowed) {
        core::Budget budget{byte_budget};
        const CheckpointStop stop{core::Checkpoint::decode, allowed};
        {
            const auto decoded = io::decode_jpeg(fixture, budget, image::ProfilePolicy::embedded,
                                                 stop.cancellation());
            if (decoded) {
                reached_native = true;
            } else {
                REQUIRE(stop.stopped());
                CHECK(decoded.error().code == core::ErrorCode::cancelled);
            }
        }
        CHECK(budget.used() == 0);
        if (reached_native) {
            break;
        }
    }
    CHECK(reached_native);
}
} // namespace docenhance::tests
