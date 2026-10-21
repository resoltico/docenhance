// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "docenhance/methods/parameter_fields.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "docenhance/record/method_request_fields.hpp"
#include "docenhance/record/method_requests.hpp"
#include "docenhance/record/scalar.hpp"
#include "docenhance/record/schema.hpp"
#include "docenhance/record/source.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "docenhance/record/source_fields.hpp"
#include "fields_fixture.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace docenhance::tests {
namespace {
template <typename T> void wire_refusal(const core::Result<T>& result) {
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::invariant);
}
} // namespace

TEST_CASE("Generated parameter fields keep one selector and resource-free kernel configuration") {
    record::SurfaceRequest surface;
    surface.selector = methods::SurfaceMode::automatic;
    auto encoded_result = record::Codec::encode(surface);
    REQUIRE(encoded_result);
    const auto encoded = std::move(*encoded_result);
    CHECK(encoded["selector"] == "auto");
    CHECK(encoded["parameters"]["cell"] == "auto");
    CHECK(encoded["parameters"]["target"] == "source");
    CHECK(!encoded["parameters"].contains("mode"));
    CHECK(!encoded["parameters"].contains("selector"));
    const auto restored = record::Codec::decode<record::SurfaceRequest>(encoded);
    REQUIRE(restored);
    CHECK(*restored == surface);
    auto invalid = encoded;
    invalid["selector"] = "missing";
    CHECK(!record::Codec::decode<record::SurfaceRequest>(invalid));
    invalid = encoded;
    invalid["parameters"]["cell"] = 2;
    CHECK(!record::Codec::decode<record::SurfaceRequest>(invalid));
    methods::WienerParameters wiener{.psf = methods::Kernel{}};
    auto kernel_result = record::Codec::encode(wiener);
    REQUIRE(kernel_result);
    const auto kernel = std::move(*kernel_result);
    CHECK(kernel["psf"].size() == 1);
    CHECK(kernel["psf"]["kind"] == "kernel");
    const auto decoded = record::Codec::decode<methods::WienerParameters>(kernel);
    REQUIRE(decoded);
    CHECK(*decoded == wiener);
    auto obsolete = kernel;
    obsolete["psf"]["path"] = "private/kernel.png";
    CHECK(!record::Codec::decode<methods::WienerParameters>(obsolete));
}
TEST_CASE("Sequence length ranges bind writer reader and emitted schema") {
    auto encoded_result = record::Codec::encode(WirePacket{});
    REQUIRE(encoded_result);
    const auto encoded = std::move(*encoded_result);
    auto invalid = encoded;
    invalid["flags"] = record::Json::array({true, false, true});
    CHECK(!record::Codec::decode<WirePacket>(invalid));
    WirePacket value;
    value.flags.push_back(true);
    wire_refusal(record::Codec::encode(value));
    auto schema_result = record::Schema::of<WirePacket>();
    REQUIRE(schema_result);
    const auto schema = std::move(*schema_result);
    CHECK(schema["properties"]["flags"]["maxItems"] == 2);
    CHECK(!schema["properties"]["flags"].contains("maximum"));
}
TEST_CASE("Floating wire ranges ignore parity while integral ranges retain it") {
    const auto schema = record::Schema::of<WireFloatingRange>();
    REQUIRE(schema);
    const auto& field = (*schema)["properties"]["value"];
    CHECK(field["minimum"] == 0);
    CHECK(field["maximum"] == 4);
    CHECK(!field.contains("not"));
    for (const double value : {0.0, 1.0, 2.0, 3.0, 4.0}) {
        const WireFloatingRange original{.value = value};
        const auto encoded = record::Codec::encode(original);
        REQUIRE(encoded);
        CHECK((*encoded)["value"] == value);
        const auto decoded = record::Codec::decode<WireFloatingRange>(*encoded);
        REQUIRE(decoded);
        CHECK(*decoded == original);
    }
    for (const double invalid : {
             -0.5,
             4.5,
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN(),
         }) {
        wire_refusal(record::Codec::encode(WireFloatingRange{.value = invalid}));
        const auto refused =
            record::Codec::decode<WireFloatingRange>(record::Json{{"value", invalid}});
        REQUIRE(!refused);
        CHECK(refused.error().code == core::ErrorCode::input);
    }
    const auto integral = record::Schema::of<WireIntegralRange>();
    REQUIRE(integral);
    const auto& numeric = (*integral)["properties"]["value"]["oneOf"][0];
    CHECK(numeric["minimum"] == 1);
    CHECK(numeric["maximum"] == 7);
    CHECK(numeric["not"]["multipleOf"] == 2);
    WireIntegralRange odd{.value = 3};
    const auto odd_encoded = record::Codec::encode(odd);
    REQUIRE(odd_encoded);
    CHECK((*odd_encoded)["value"] == 3);
    const auto odd_decoded = record::Codec::decode<WireIntegralRange>(*odd_encoded);
    REQUIRE(odd_decoded);
    CHECK(*odd_decoded == odd);
    auto even_encoded = *odd_encoded;
    even_encoded["value"] = 2;
    CHECK(!record::Codec::decode<WireIntegralRange>(even_encoded));
    odd.value = 2;
    wire_refusal(record::Codec::encode(odd));
    const WireIntegralRange automatic;
    const auto auto_encoded = record::Codec::encode(automatic);
    REQUIRE(auto_encoded);
    CHECK((*auto_encoded)["value"] == "auto");
    const auto auto_decoded = record::Codec::decode<WireIntegralRange>(*auto_encoded);
    REQUIRE(auto_decoded);
    CHECK(*auto_decoded == automatic);
}
TEST_CASE("Model read overloads cannot bypass typed wire admission") {
    const record::Json healthy{{"kind", "trap"}, {"value", 2}};
    const auto restored = record::Codec::decode<WireReadTrap>(healthy);
    REQUIRE(restored);
    CHECK(restored->value == 2);
    using TrapVariant = std::variant<WireReadTrap, WirePng>;
    CHECK(record::Codec::decode<std::optional<WireReadTrap>>(healthy));
    CHECK(record::Codec::decode<std::vector<WireReadTrap>>(record::Json::array({healthy})));
    CHECK(record::Codec::decode<TrapVariant>(healthy));
    for (const auto& invalid : {
             record::Json{{"kind", "trap"}, {"value", 2}, {"extra", 0}},
             record::Json{{"kind", "other"}, {"value", 2}},
             record::Json{{"kind", "trap"}, {"value", 0}},
         }) {
        WireReadTrap bypass;
        REQUIRE(read(invalid, bypass));
        CHECK(!record::Codec::decode<WireReadTrap>(invalid));
        CHECK(!record::Codec::decode<std::optional<WireReadTrap>>(invalid));
        CHECK(!record::Codec::decode<std::vector<WireReadTrap>>(record::Json::array({invalid})));
        CHECK(!record::Codec::decode<TrapVariant>(invalid));
    }
}
TEST_CASE("Finite PNG source domains preserve holes and contextual color precision") {
    record::PngContainer value{.width = 1, .height = 1};
    for (const auto precision : {1U, 2U, 4U, 8U, 16U}) {
        value.precision = precision;
        const auto healthy_1 = record::Codec::encode(value);
        REQUIRE(healthy_1);
        CHECK(record::Codec::decode<record::PngContainer>(*healthy_1));
    }
    value.precision = 8;
    for (const auto color : {0U, 2U, 3U, 4U, 6U}) {
        value.color_type = color;
        const auto healthy_2 = record::Codec::encode(value);
        REQUIRE(healthy_2);
        CHECK(record::Codec::decode<record::PngContainer>(*healthy_2));
    }
    value.color_type = 0;
    auto encoded_result = record::Codec::encode(value);
    REQUIRE(encoded_result);
    const auto encoded = std::move(*encoded_result);
    for (const auto& hole : record::Json::array({3, true, 1.5, -1, 4294967296ULL})) {
        auto invalid = encoded;
        invalid["precision"] = hole;
        CHECK(!record::Codec::decode<record::PngContainer>(invalid));
    }
    for (const auto color : {1U, 5U}) {
        auto invalid = encoded;
        invalid["color_type"] = color;
        CHECK(!record::Codec::decode<record::PngContainer>(invalid));
        value.color_type = color;
        wire_refusal(record::Codec::encode(value));
    }
    value.color_type = 0;
    value.precision = 3;
    wire_refusal(record::Codec::encode(value));
    value.color_type = 3;
    value.precision = 16;
    wire_refusal(record::Codec::encode(value));
    auto invalid = encoded;
    invalid["color_type"] = 3;
    invalid["precision"] = 16;
    CHECK(!record::Codec::decode<record::PngContainer>(invalid));
    auto schema_result = record::Schema::of<record::PngContainer>();
    REQUIRE(schema_result);
    const auto schema = (*schema_result)["properties"];
    CHECK(schema["precision"]["enum"] == record::Json::array({1, 2, 4, 8, 16}));
    CHECK(schema["color_type"]["enum"] == record::Json::array({0, 2, 3, 4, 6}));
    CHECK(schema["precision"]["type"] == "integer");
    auto scalar = record::ScalarCodec::schema<unsigned>();
    REQUIRE(scalar);
    REQUIRE(record::ScalarCodec::constrain(*scalar, core::Choices{std::array{1U, 2U}}));
    wire_refusal(record::ScalarCodec::constrain(*scalar, core::Choices{std::array{1U, 1U}}));
    wire_refusal(
        record::ScalarCodec::constrain(*scalar, core::Choices{std::array{WireMode::unlisted}}));
    wire_refusal(record::ScalarCodec::constrain(
        *scalar, core::Choices{std::array{std::numeric_limits<double>::infinity()}}));
}
TEST_CASE("Opaque PNG channel choices retain decoded alpha source admission") {
    const auto schema = record::Schema::of<image::PngShape>();
    REQUIRE(schema);
    CHECK((*schema)["properties"]["channels"]["enum"] == record::Json::array({1, 3}));
    for (const auto model : {image::SampleModel::gray_alpha, image::SampleModel::rgba}) {
        const image::RasterShape decoded{.width = 1, .height = 1, .model = model};
        const auto healthy_3 = record::Codec::encode(decoded);
        REQUIRE(healthy_3);
        CHECK(record::Codec::decode<image::RasterShape>(*healthy_3));
        const image::PngShape output{
            .width = 1,
            .height = 1,
            .model = model,
            .depth = image::PngDepth(),
        };
        wire_refusal(record::Codec::encode(output));
        auto invalid_result = record::Codec::encode(
            image::PngShape{.width = 1, .height = 1, .depth = image::PngDepth()});
        REQUIRE(invalid_result);
        auto invalid = std::move(*invalid_result);
        invalid["channels"] = image::components(model);
        CHECK(!record::Codec::decode<image::PngShape>(invalid));
    }
}
} // namespace docenhance::tests
