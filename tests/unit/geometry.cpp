// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/image/geometry.hpp"

#include "cancellation_probe.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "stub_verifier.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

namespace docenhance::tests {
namespace {
image::QuarterTurn required_turn(unsigned degrees) {
    const auto turn = image::QuarterTurn::from_degrees(degrees);
    if (!turn) {
        FAIL("Expected an admitted quarter turn");
        return {};
    }
    return *turn;
}
} // namespace
TEST_CASE("Quarter turns admit only exact clockwise degree alternatives", "[geometry]") {
    static_assert(!std::is_constructible_v<image::QuarterTurn, unsigned>);
    for (const auto degrees : {1U, 45U, 89U, 91U, 360U, std::numeric_limits<unsigned>::max()}) {
        CHECK(!image::QuarterTurn::from_degrees(degrees));
    }
    CHECK(image::QuarterTurn{}.degrees() == 0);
    for (const auto degrees : {0U, 90U, 180U, 270U}) {
        const auto turn = image::QuarterTurn::from_degrees(degrees);
        if (!turn) {
            FAIL("Every canonical quarter turn must be admitted");
            return;
        }
        CHECK(turn->degrees() == degrees);
        CHECK(turn->swaps_axes() == (degrees == 90 || degrees == 270));
    }
}

TEST_CASE("Exact rotation writes only pixels and preserves independent asymmetric samples",
          "[geometry]") {
    const std::array<std::uint8_t, 8> source{1, 2, 3, 99, 4, 5, 6, 99};
    const auto input = image::PlaneView<const std::uint8_t>::create(
        source, {.width = 3, .height = 2, .stride = 4});
    REQUIRE(input);
    constexpr std::array expected{
        std::array<std::uint8_t, 6>{1, 2, 3, 4, 5, 6},
        std::array<std::uint8_t, 6>{4, 1, 5, 2, 6, 3},
        std::array<std::uint8_t, 6>{6, 5, 4, 3, 2, 1},
        std::array<std::uint8_t, 6>{3, 6, 2, 5, 1, 4},
    };
    for (unsigned index = 0; index < expected.size(); ++index) {
        const auto rotation = required_turn(index * 90);
        const auto shape = image::rotated_shape({.width = 3, .height = 2}, rotation);
        CHECK(shape.width == (rotation.swaps_axes() ? 2U : 3U));
        CHECK(shape.height == (rotation.swaps_axes() ? 3U : 2U));
        std::array<std::uint8_t, 12> output{};
        output.fill(77);
        const auto destination = image::PlaneView<std::uint8_t>::create(
            output, {.width = shape.width, .height = shape.height, .stride = 4});
        REQUIRE(destination);
        REQUIRE(image::rotate_plane(*input, *destination, rotation));
        for (std::uint32_t y = 0; y < shape.height; ++y) {
            for (std::uint32_t x = 0; x < shape.width; ++x) {
                CHECK(destination->row(y)[x] == expected.at(index).at((y * shape.width) + x));
            }
            for (std::uint32_t x = shape.width; x < 4; ++x) {
                CHECK(output.at((y * 4) + x) == 77);
            }
        }
    }
}

TEST_CASE("Rotation refuses overlap and incompatible dimensions before cancellation",
          "[geometry]") {
    std::array<std::uint8_t, 6> source{1, 2, 3, 4, 5, 6};
    std::array<std::uint8_t, 6> output{};
    const auto input = image::PlaneView<const std::uint8_t>::create(
        source, {.width = 3, .height = 2, .stride = 3});
    const auto same =
        image::PlaneView<std::uint8_t>::create(source, {.width = 3, .height = 2, .stride = 3});
    const auto wrong =
        image::PlaneView<std::uint8_t>::create(output, {.width = 3, .height = 2, .stride = 3});
    REQUIRE(input);
    REQUIRE(same);
    REQUIRE(wrong);
    const auto turn = required_turn(90);
    const CheckpointStop stop{core::Checkpoint::processing, 0};
    const auto overlap = image::rotate_plane(*input, *same, {}, stop.cancellation());
    REQUIRE(!overlap);
    CHECK(overlap.error().code == core::ErrorCode::argument);
    const auto shape = image::rotate_plane(*input, *wrong, turn, stop.cancellation());
    REQUIRE(!shape);
    CHECK(shape.error().code == core::ErrorCode::argument);
    CHECK(CheckpointStop::visits() == 0);
    CHECK(source == std::array<std::uint8_t, 6>{1, 2, 3, 4, 5, 6});
    CHECK(output == std::array<std::uint8_t, 6>{});
}

TEST_CASE("Rotation checkpoints interrupt within a wide row without timing sleeps", "[geometry]") {
    constexpr std::uint32_t width = 4096;
    std::array<std::uint8_t, width> source{};
    std::array<std::uint8_t, width> output{};
    source.fill(1);
    output.fill(77);
    const auto input = image::PlaneView<const std::uint8_t>::create(
        source, {.width = width, .height = 1, .stride = width});
    const auto destination = image::PlaneView<std::uint8_t>::create(
        output, {.width = width, .height = 1, .stride = width});
    REQUIRE(input);
    REQUIRE(destination);
    const CheckpointStop stop{core::Checkpoint::processing, 1};
    const auto result = image::rotate_plane(*input, *destination, {}, stop.cancellation());
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::cancelled);
    CHECK(CheckpointStop::stopped());
    CHECK(output.front() == 1);
    CHECK(output.back() == 77);
}

namespace {
class GeometryProcessor final : public app::Processor {
  public:
    explicit GeometryProcessor(app::ProcessResult value) : value_(std::move(value)) {}
    app::ProcessResult process(const app::ProcessRequest& /*request*/,
                               const core::Cancellation& /*cancellation*/) override {
        return value_;
    }

  private:
    app::ProcessResult value_;
};
app::PublishedContinuous geometry_observation() {
    app::PublishedContinuous value;
    value.output = "output/result.png";
    value.run = std::string(32, 'a');
    value.record = {.sha256 = std::string(64, 'b'), .bytes = 512};
    value.conversion.source = {.width = 2, .height = 2};
    value.conversion.output = value.conversion.source;
    value.conversion.assumed_transfer = true;
    value.conversion.verified = true;
    value.illumination.complete = true;
    value.illumination.eligible_samples = 4;
    value.contrast.complete = true;
    value.contrast.eligible_samples = 4;
    value.sharpening.complete = true;
    value.sharpening.eligible_samples = 4;
    value.restoration.complete = true;
    value.restoration.eligible_samples = 4;
    value.denoising.complete = true;
    value.denoising.eligible_samples = 4;
    return value;
}
void check_rotation_observation(bool binary, unsigned degrees, unsigned observed) {
    UnusedVerifier verifier;
    contract::Invocation request;
    request.command = contract::Command::process;
    request.subject = "input.png";
    request.output_directory = "output";
    request.output_mode = binary ? "bw" : "preserve";
    request.rotate = std::to_string(degrees);
    const auto turn = required_turn(observed);
    app::ProcessResult published;
    if (binary) {
        published = app::PublishedBinary{
            .output = "output/result.png",
            .run = std::string(32, 'a'),
            .record = {.sha256 = std::string(64, 'b'), .bytes = 512},
            .rotation = turn,
        };
    } else {
        auto continuous = geometry_observation();
        continuous.conversion.rotation = turn;
        published = continuous;
    }
    GeometryProcessor processor{published};
    const auto result = app::dispatch(request, processor, verifier);
    if (observed == degrees) {
        CHECK(result.exit_code() == core::ExitCode::success);
    } else {
        const auto failure = std::get<app::Failure>(result.payload).error;
        CHECK(failure.code == core::ErrorCode::publication_unknown);
        CHECK(failure.publication == core::Publication::unknown);
    }
}
} // namespace
TEST_CASE("Processing rotation observations must agree with admitted requests", "[geometry][app]") {
    for (const bool binary : {false, true}) {
        for (const auto degrees : {0U, 90U}) {
            for (const auto observed : {0U, 90U}) {
                check_rotation_observation(binary, degrees, observed);
            }
        }
    }
}
} // namespace docenhance::tests
