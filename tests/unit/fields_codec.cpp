// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/schema.hpp"
#include "fields_fixture.hpp"
#include "png_depth_fixture.hpp"
#include "schema_example.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
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

TEST_CASE("Typed wire fields round trip numeric and empty scalar choices") {
    WirePacket value;
    auto encoded_result = record::Codec::encode(value);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    CHECK(encoded["measure"].is_null());
    CHECK(encoded["cell"] == "auto");
    CHECK(encoded["target"] == "source");
    CHECK(encoded["container"]["format"] == "png");
    const auto empty = record::Codec::decode<WirePacket>(encoded);
    REQUIRE(empty);
    CHECK(*empty == value);
    value.cell = 31;
    value.target = 0.5;
    value.measure = 0.25;
    value.container = WireJpeg{.height = 17};
    encoded_result = record::Codec::encode(value);
    REQUIRE(encoded_result);
    encoded = std::move(*encoded_result);
    const auto numeric = record::Codec::decode<WirePacket>(encoded);
    REQUIRE(numeric);
    CHECK(*numeric == value);
    const auto schema = record::Schema::of<WirePacket>();
    REQUIRE(schema);
    CHECK((*schema)["properties"]["count"]["maximum"] == std::numeric_limits<std::uint8_t>::max());
}

TEST_CASE("Typed fixed objects require null fields and refuse unknown keys") {
    auto encoded_result = record::Codec::encode(WirePacket{});
    REQUIRE(encoded_result);
    const auto encoded = std::move(*encoded_result);
    for (const auto* const key : {
             "identity",
             "count",
             "measure",
             "cell",
             "target",
             "mode",
             "samples",
             "flags",
             "container",
         }) {
        auto missing = encoded;
        missing.erase(key);
        CHECK(!record::Codec::decode<WirePacket>(missing));
    }
    auto changed = encoded;
    changed["unknown"] = nullptr;
    CHECK(!record::Codec::decode<WirePacket>(changed));
    changed = encoded;
    changed["cell"] = nullptr;
    CHECK(!record::Codec::decode<WirePacket>(changed));
    changed["cell"] = "source";
    CHECK(!record::Codec::decode<WirePacket>(changed));
    changed["cell"] = "31";
    CHECK(!record::Codec::decode<WirePacket>(changed));
}

TEST_CASE("Typed numeric decoding refuses narrowing booleans and nonfinite values") {
    auto encoded_result = record::Codec::encode(WirePacket{});
    REQUIRE(encoded_result);
    const auto encoded = std::move(*encoded_result);
    for (const auto& invalid : record::Json::array({-1, 256, 1.0, true, "1"})) {
        auto changed = encoded;
        changed["count"] = invalid;
        CHECK(!record::Codec::decode<WirePacket>(changed));
    }
    for (const auto invalid : {
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN(),
             -0.1,
             1.1,
         }) {
        auto changed = encoded;
        changed["measure"] = invalid;
        CHECK(!record::Codec::decode<WirePacket>(changed));
        WirePacket value;
        value.measure = invalid;
        wire_refusal(record::Codec::encode(value));
    }
}

TEST_CASE("Typed alternatives require their declared discriminator and payload") {
    auto encoded_result = record::Codec::encode(WirePacket{});
    REQUIRE(encoded_result);
    const auto encoded = std::move(*encoded_result);
    for (const auto& source : record::Json::array({
             {{"format", "png"}, {"height", 1}},
             {{"format", "tiff"}, {"width", 1}},
             {{"method", "png"}, {"width", 1}},
             {{"format", "png"}, {"width", 1}, {"extra", 0}},
         })) {
        auto changed = encoded;
        changed["container"] = source;
        CHECK(!record::Codec::decode<WirePacket>(changed));
    }
    auto changed = encoded;
    changed["mode"] = "missing";
    CHECK(!record::Codec::decode<WirePacket>(changed));
    changed = encoded;
    changed["samples"] = record::Json::array({0});
    CHECK(!record::Codec::decode<WirePacket>(changed));
    WirePacket value;
    value.mode = WireMode::unlisted;
    wire_refusal(record::Codec::encode(value));
}

TEST_CASE("Sparse completed reports remain closed and family specific") {
    WireCompleted completed{
        .illumination = WireLight{.eligible = 2, .changed = 1},
        .denoising = std::nullopt,
    };
    auto encoded_result = record::Codec::encode(completed);
    REQUIRE(encoded_result);
    const auto encoded = std::move(*encoded_result);
    CHECK(encoded.size() == 1);
    CHECK(!encoded.contains("denoising"));
    const auto decoded = record::Codec::decode<WireCompleted>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == completed);
    auto changed = encoded;
    changed["denoising"] = nullptr;
    CHECK(!record::Codec::decode<WireCompleted>(changed));
    changed = encoded;
    changed["illumination"]["method"] = "D01";
    CHECK(!record::Codec::decode<WireCompleted>(changed));
    changed = encoded;
    const auto noise = record::Codec::encode(WireNoise{});
    REQUIRE(noise);
    changed["unknown"] = *noise;
    CHECK(!record::Codec::decode<WireCompleted>(changed));
    CHECK(record::Codec::decode<WireCompleted>(record::Json::object()));
    auto fixed_result = record::Codec::encode(WireStages{});
    REQUIRE(fixed_result);
    const auto fixed = std::move(*fixed_result);
    CHECK(fixed.size() == 2);
    CHECK(fixed["illumination"].is_null());
    CHECK(fixed["denoising"].is_null());
    CHECK(!record::Codec::decode<WireStages>(record::Json::object()));
}

TEST_CASE("Wire owner cross field invariants apply during writing and reading") {
    const WireLight invalid{.eligible = 1, .changed = 2};
    wire_refusal(record::Codec::encode(invalid));
    CHECK(!record::Codec::decode<WireLight>({{"method", "I01"}, {"eligible", 1}, {"changed", 2}}));
    CHECK(record::Codec::decode<WireLight>({{"method", "I01"}, {"eligible", 2}, {"changed", 1}}));
}

TEST_CASE("Wire variants guard actual valueless input before visiting") {
    struct ThrowingText {
        explicit operator WireText() const {
            throw std::runtime_error("Injected wire text construction failure");
        }
    };
    std::variant<WirePng, WireText> value;
    CHECK_THROWS_AS(value.emplace<WireText>(ThrowingText{}), std::runtime_error);
    REQUIRE(value.valueless_by_exception());
    wire_refusal(record::Codec::encode(value));
}

TEST_CASE("Wire strings preserve identity bytes and refuse malformed UTF-8") {
    const WireText original{.text = "caf\xc3\xa9"};
    const auto encoded = record::Codec::encode(original);
    REQUIRE(encoded);
    const auto decoded = record::Codec::decode<WireText>(*encoded);
    REQUIRE(decoded);
    CHECK(decoded->text == original.text);
    wire_refusal(record::Codec::encode(WireText{.text = "\xff"}));
    CHECK(!record::Codec::decode<WireText>({{"format", "text"}, {"text", "\xff"}}));
    auto packet_result = record::Codec::encode(WirePacket{});
    REQUIRE(packet_result);
    auto packet = std::move(*packet_result);
    packet["identity"] = "DEADBEEF";
    CHECK(!record::Codec::decode<WirePacket>(packet));
}

TEST_CASE("Raw wire admission rejects escaped duplicate keys before DOM construction") {
    const auto encoded = record::Codec::encode(WirePacket{});
    REQUIRE(encoded);
    auto text = encoded->dump();
    text.insert(1, R"("\u0063ount":1,)");
    const auto refused =
        record::Codec::parse<WirePacket>(text, {.bytes = 4096, .depth = 8, .events = 128});
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::input);
    CHECK(refused.error().message.contains("unique keys"));
    CHECK(record::admit_json("{\"a\":1}", {.bytes = 7, .depth = 1, .events = 4}));
    CHECK(!record::admit_json("{\"a\":1}", {.bytes = 6, .depth = 1, .events = 4}));
    CHECK(!record::admit_json("{\"a\":1}", {.bytes = 7, .depth = 1, .events = 3}));
    CHECK(record::admit_json("[[0]]", {.bytes = 5, .depth = 2, .events = 5}));
    CHECK(!record::admit_json("[[0]]", {.bytes = 5, .depth = 1, .events = 5}));
}

TEST_CASE("Nullable unions use one canonical absence and empty objects stay distinct") {
    using Value = std::optional<std::variant<std::monostate, WirePng>>;
    const Value absent;
    const auto absent_encoded = record::Codec::encode(absent);
    REQUIRE(absent_encoded);
    CHECK(absent_encoded->is_null());
    const auto decoded = record::Codec::decode<Value>(nullptr);
    REQUIRE(decoded);
    CHECK(!decoded->has_value());
    const Value redundant{std::variant<std::monostate, WirePng>{}};
    wire_refusal(record::Codec::encode(redundant));
    const auto empty_encoded = record::Codec::encode(WireEmpty{});
    REQUIRE(empty_encoded);
    CHECK(empty_encoded->is_object());
    CHECK(record::Codec::decode<WireEmpty>(record::Json::object()));
    CHECK(!record::Codec::decode<WireEmpty>({{"extra", nullptr}}));
}
TEST_CASE("Physical PNG precision stays separate from decoded sample precision") {
    CHECK(!record::Codec::decode<image::SampleDepth>(1));
    CHECK(record::Codec::decode<image::PngDepth>(1));
    for (const auto bits : {8U, 16U}) {
        const auto decoded = record::Codec::decode<image::SampleDepth>(bits);
        const auto encoded = record::Codec::decode<image::PngDepth>(bits);
        REQUIRE(decoded);
        REQUIRE(encoded);
        CHECK(decoded->bits() == bits);
        CHECK(encoded->bits() == bits);
    }
    for (const auto& invalid : record::Json::array({0, 2, 4, 32, -1, true, 8.0, "8"})) {
        CHECK(!record::Codec::decode<image::SampleDepth>(invalid));
        CHECK(!record::Codec::decode<image::PngDepth>(invalid));
    }
    CHECK(!record::Codec::decode<image::Orientation>(0));
    CHECK(!record::Codec::decode<image::Orientation>(9));
    CHECK(record::Codec::decode<image::Orientation>(8));
    CHECK(!record::Codec::decode<image::QuarterTurn>(360));
    CHECK(!record::Codec::decode<image::QuarterTurn>(45));
    CHECK(record::Codec::decode<image::QuarterTurn>(270));
    const image::PngShape shape{.width = 1, .height = 1, .depth = fixture_png_depth(1)};
    const auto healthy_1 = record::Codec::encode(shape);
    REQUIRE(healthy_1);
    CHECK(record::Codec::decode<image::PngShape>(*healthy_1));
    auto encoded_result = record::Codec::encode(shape);
    REQUIRE(encoded_result);
    auto encoded = std::move(*encoded_result);
    encoded["channels"] = 3;
    CHECK(!record::Codec::decode<image::PngShape>(encoded));
    encoded["channels"] = 1;
    encoded["width"] = 0;
    CHECK(!record::Codec::decode<image::PngShape>(encoded));
}

TEST_CASE("Interpretation clipping counts retain decoded category across output modes") {
    image::InterpretationReport value{
        .decoded = {.width = 2, .height = 1, .model = image::SampleModel::rgb},
        .output =
            {
                .width = 2,
                .height = 1,
                .depth = image::PngDepth(),
            },
        .orientation = image::Orientation(),
        .rotation = image::QuarterTurn(),
        .resolution = std::nullopt,
        .clipped_components = 6,
    };
    CHECK(image::validate(value));
    auto encoded_result = record::Codec::encode(value);
    REQUIRE(encoded_result);
    const auto encoded = std::move(*encoded_result);
    const auto decoded = record::Codec::decode<image::InterpretationReport>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == value);
    auto invalid = encoded;
    invalid["clipped_components"] = 7;
    CHECK(!record::Codec::decode<image::InterpretationReport>(invalid));
    invalid = encoded;
    invalid["decoded"]["channels"] = 1;
    CHECK(!record::Codec::decode<image::InterpretationReport>(invalid));
    invalid["clipped_components"] = 2;
    CHECK(record::Codec::decode<image::InterpretationReport>(invalid));
    invalid["alpha_composited_pixels"] = 1;
    CHECK(!record::Codec::decode<image::InterpretationReport>(invalid));
    invalid["decoded"]["channels"] = 2;
    CHECK(record::Codec::decode<image::InterpretationReport>(invalid));
    invalid["alpha_composited_pixels"] = 3;
    CHECK(!record::Codec::decode<image::InterpretationReport>(invalid));
    const auto interpretation = record::Schema::of<image::InterpretationReport>();
    REQUIRE(interpretation);
    CHECK((*interpretation)["properties"]["clipped_components"]["maximum"] ==
          3 * image::source_pixels_max);
    const auto counts = record::Schema::of<methods::PixelCounts>();
    REQUIRE(counts);
    CHECK((*counts)["properties"]["changed"]["maximum"] == image::source_pixels_max);
}

TEST_CASE("Binary classification carries no continuous precision reduction claim") {
    image::InterpretationReport value{
        .decoded = {.width = 1, .height = 1, .depth = image::SampleDepth::word()},
        .output = {.width = 1, .height = 1, .depth = fixture_png_depth(1)},
        .orientation = image::Orientation(),
        .rotation = image::QuarterTurn(),
        .resolution = std::nullopt,
    };
    CHECK(image::validate(value));
    value.depth_reduced = true;
    CHECK(!image::validate(value));
    value.output.depth = fixture_png_depth(8);
    CHECK(image::validate(value));
    value.depth_reduced = false;
    CHECK(!image::validate(value));
    value.output.depth = fixture_png_depth(16);
    CHECK(image::validate(value));
}
TEST_CASE("Nested wire failures preserve the first refusing child across later successes") {
    WireStages stages{
        .illumination = WireLight{.eligible = 1, .changed = 2},
        .denoising = WireNoise{.value = std::numeric_limits<double>::infinity()},
    };
    const auto first = record::Codec::encode(*stages.illumination);
    wire_refusal(first);
    const auto nested = record::Codec::encode(stages);
    wire_refusal(nested);
    CHECK(nested.error().message == first.error().message);
    stages.denoising->value = 0;
    const auto followed_by_success = record::Codec::encode(stages);
    wire_refusal(followed_by_success);
    CHECK(followed_by_success.error().message == first.error().message);
    const std::array<WireLight, 2> sequence{*stages.illumination, WireLight{}};
    const auto element = record::Codec::encode(sequence);
    wire_refusal(element);
    CHECK(element.error().message == first.error().message);
    const std::variant<WireLight, WireNoise> alternative{*stages.illumination};
    const auto selected = record::Codec::encode(alternative);
    wire_refusal(selected);
    CHECK(selected.error().message == first.error().message);
    stages.illumination->changed = 1;
    REQUIRE(record::Codec::encode(stages));
}
TEST_CASE("Unsupported collection schema refuses through optional sequence and variant shapes") {
    REQUIRE(record::Codec::encode(WireOddCollection{}));
    const auto unsupported = record::Schema::of<WireOddCollection>();
    wire_refusal(unsupported);
    const auto optional = record::Schema::of<std::optional<WireOddCollection>>();
    wire_refusal(optional);
    CHECK(optional.error().message == unsupported.error().message);
    const auto sequence = record::Schema::of<std::vector<WireOddCollection>>();
    wire_refusal(sequence);
    CHECK(sequence.error().message == unsupported.error().message);
    const auto alternative = record::Schema::of<std::variant<WireOddCollection, WirePng>>();
    wire_refusal(alternative);
    CHECK(alternative.error().message == unsupported.error().message);
    REQUIRE(record::Schema::of<std::variant<WirePng, WireJpeg>>());
}
TEST_CASE("Schema example construction refuses before appending a successful fixture") {
    auto examples = record::Json::array();
    const auto bad_value = schema_example(examples, "invalid light", WireLight{.changed = 2});
    wire_refusal(bad_value);
    CHECK(examples.empty());
    const auto bad_schema = schema_example(examples, "unsupported odd", WireOddCollection{});
    wire_refusal(bad_schema);
    CHECK(examples.empty());
    REQUIRE(schema_example(examples, "healthy light", WireLight{}));
    REQUIRE(examples.size() == 1);
    CHECK(examples[0]["name"] == "healthy light");
}
} // namespace docenhance::tests
