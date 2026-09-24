// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "cancellation_probe.hpp"
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "png_fixture.hpp"
#include "png_rows.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace docenhance::tests {
namespace {
constexpr std::size_t byte_budget = std::size_t{32} * 1024 * 1024;
class Directory {
  public:
    Directory()
        : path(std::filesystem::temp_directory_path() /
               ("docenhance-color-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        REQUIRE(std::filesystem::create_directory(path));
    }
    Directory(const Directory&) = delete;
    Directory& operator=(const Directory&) = delete;
    Directory(Directory&&) = delete;
    Directory& operator=(Directory&&) = delete;
    ~Directory() {
        try {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        } catch (...) {
            std::terminate();
        }
    }
    std::filesystem::path path;
};
std::string spelling(const std::filesystem::path& path) {
    std::string result;
    for (const auto byte : path.u8string()) {
        result.push_back(static_cast<char>(byte));
    }
    return result;
}
image::Raster source(core::Budget& budget) {
    const auto bytes = make_gray_png({
        .width = 8,
        .height = 8,
        .depth = 8,
        .interlaced = false,
        .samples = std::vector<std::uint8_t>(64, 127),
    });
    return io::decode_png_raster(bytes, budget, image::ProfilePolicy::embedded).value();
}
class AlteredRows final : public image::RowSource {
  public:
    explicit AlteredRows(image::RowSource& source) : source_(source) {}
    [[nodiscard]] image::OutputDescriptor descriptor() const noexcept override {
        auto descriptor = source_.get().descriptor();
        if (different_metadata_) {
            descriptor.resolution = image::Resolution{.x = 1000, .y = 1000};
        }
        return descriptor;
    }
    [[nodiscard]] core::Result<void> row(std::uint32_t index, std::span<std::uint8_t> bytes,
                                         image::RowUse use) override {
        auto result = source_.get().row(index, bytes, use);
        if (result && use == image::RowUse::verification) {
            bytes.front() ^= std::uint8_t{1};
        }
        return result;
    }
    void alter_metadata() noexcept {
        different_metadata_ = true;
    }

  private:
    std::reference_wrapper<image::RowSource> source_;
    bool different_metadata_ = false;
};
} // namespace
TEST_CASE("Continuous output verification rejects wrong samples before publication",
          "[continuous][io]") {
    const Directory directory;
    core::Budget budget{byte_budget};
    const auto raster = source(budget);
    auto converter =
        color::Converter::create(raster, image::Continuous::create({}).value(), budget).value();
    AlteredRows altered{*converter};
    const auto held = budget.used();
    const auto result = io::publish_png_rows(spelling(directory.path / "result"), altered, budget);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == core::ErrorCode::output_verify);
    CHECK(result.error().publication == core::Publication::not_published);
    CHECK(std::filesystem::is_empty(directory.path));
    CHECK(budget.used() == held);
}
TEST_CASE("Independent output verification checks metadata not just pixels", "[continuous][io]") {
    const Directory directory;
    core::Budget budget{byte_budget};
    const auto raster = source(budget);
    auto converter =
        color::Converter::create(raster, image::Continuous::create({}).value(), budget).value();
    const auto path = directory.path / "encoded.png";
    REQUIRE(io::encode_png_rows(path, *converter, budget, {}));
    AlteredRows altered{*converter};
    altered.alter_metadata();
    const auto result = io::verify_png_rows(path, altered, budget, {});
    REQUIRE_FALSE(result);
    CHECK(result.error().code == core::ErrorCode::output_verify);
}
TEST_CASE("Cancellation during continuous verification never commits staged output",
          "[continuous][cancellation]") {
    const Directory directory;
    core::Budget budget{byte_budget};
    const auto raster = source(budget);
    auto converter =
        color::Converter::create(raster, image::Continuous::create({}).value(), budget).value();
    const auto held = budget.used();
    bool completed = false;
    constexpr std::size_t attempts = 256;
    for (std::size_t after = 0; after < attempts; ++after) {
        const CheckpointStop stop{core::Checkpoint::verification, after};
        const auto result = io::publish_png_rows(spelling(directory.path / "output"), *converter,
                                                 budget, stop.cancellation());
        if (CheckpointStop::stopped()) {
            REQUIRE_FALSE(result);
            CHECK(result.error().code == core::ErrorCode::cancelled);
            CHECK(result.error().publication == core::Publication::not_published);
            CHECK(std::filesystem::is_empty(directory.path));
        } else {
            REQUIRE(result);
            completed = true;
        }
        CHECK(budget.used() == held);
        if (completed) {
            break;
        }
    }
    REQUIRE(completed);
}
} // namespace docenhance::tests
