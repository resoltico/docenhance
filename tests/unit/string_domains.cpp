// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/bundle_path.hpp"
#include "docenhance/core/fields.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/scalar.hpp"
#include "docenhance/record/schema.hpp"
#include "string_fields_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>

namespace docenhance::tests {
namespace {
template <typename T> void wire_refusal(const core::Result<T>& result) {
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::invariant);
}
} // namespace

TEST_CASE("String policies retain owned identity bytes and closed primitive admission") {
    const WireStrings value;
    auto encoded_result = record::Codec::encode(value);
    REQUIRE(encoded_result);
    const auto encoded = std::move(*encoded_result);
    const auto decoded = record::Codec::decode<WireStrings>(encoded);
    REQUIRE(decoded);
    CHECK(*decoded == value);
    for (const auto* const key : {"text", "instant", "path", "literal"}) {
        for (const auto& wrong : record::Json::array(
                 {nullptr, true, 1, 1.5, record::Json::array(), record::Json::object()})) {
            auto changed = encoded;
            changed[key] = wrong;
            CHECK(!record::Codec::decode<WireStrings>(changed));
        }
        auto changed = encoded;
        changed[key] = "\xff";
        CHECK(!record::Codec::decode<WireStrings>(changed));
        changed = encoded;
        changed.erase(key);
        CHECK(!record::Codec::decode<WireStrings>(changed));
    }
    auto changed = encoded;
    changed["unknown"] = "owned";
    CHECK(!record::Codec::decode<WireStrings>(changed));
}
TEST_CASE("Text policies refuse empty and NUL while literals retain deliberate empty values") {
    for (const auto& text : {std::string{}, std::string{"a\0b", 3}, std::string{"\xff"}}) {
        WireStrings value;
        value.text = text;
        wire_refusal(record::Codec::encode(value));
        auto changed_result = record::Codec::encode(WireStrings{});
        REQUIRE(changed_result);
        auto changed = std::move(*changed_result);
        changed["text"] = text;
        CHECK(!record::Codec::decode<WireStrings>(changed));
    }
    WireStrings value;
    value.text = "a\n/b\\c:d";
    const auto healthy_1 = record::Codec::encode(value);
    REQUIRE(healthy_1);
    CHECK(record::Codec::decode<WireStrings>(*healthy_1));
    value.literal = "different";
    wire_refusal(record::Codec::encode(value));
    auto changed_result = record::Codec::encode(WireStrings{});
    REQUIRE(changed_result);
    auto changed = std::move(*changed_result);
    changed["literal"] = value.literal;
    CHECK(!record::Codec::decode<WireStrings>(changed));
    const auto healthy_2 = record::Codec::encode(WireEmptyLiteral{});
    REQUIRE(healthy_2);
    CHECK(record::Codec::decode<WireEmptyLiteral>(*healthy_2));
    CHECK(!record::Codec::decode<WireEmptyLiteral>({{"text", "not empty"}}));
    auto schema = record::ScalarCodec::schema<std::string>();
    REQUIRE(schema);
    REQUIRE(record::ScalarCodec::constrain(*schema, core::Literal{.text = "healthy"}));
    wire_refusal(record::ScalarCodec::constrain(*schema, core::Literal{.text = "\xff"}));
}
TEST_CASE("Instant native admission retains calendar and fixed ASCII UTC identity") {
    auto encoded_result = record::Codec::encode(WireStrings{});
    REQUIRE(encoded_result);
    const auto encoded = std::move(*encoded_result);
    for (const auto* const instant : {
             "2023-02-29T23:59:59Z",
             "0000-01-01T00:00:00Z",
             "2024-02-29T24:00:00Z",
             "2024-02-29T23:60:00Z",
             "2024-02-29T23:59:60Z",
             "2024-02-29t23:59:59Z",
             "2024-02-29T23:59:59Z\n",
         }) {
        auto changed = encoded;
        changed["instant"] = instant;
        CHECK(!record::Codec::decode<WireStrings>(changed));
        WireStrings value;
        value.instant = instant;
        wire_refusal(record::Codec::encode(value));
    }
    for (const auto* const instant :
         {"2000-02-29T00:00:00Z", "0001-01-01T00:00:00Z", "9999-12-31T23:59:59Z"}) {
        auto changed = encoded;
        changed["instant"] = instant;
        CHECK(record::Codec::decode<WireStrings>(changed));
    }
}
TEST_CASE("Bundle path domains preserve LF components and enforce UTF8 byte ceiling") {
    auto encoded_result = record::Codec::encode(WireStrings{});
    REQUIRE(encoded_result);
    const auto encoded = std::move(*encoded_result);
    for (const auto& path : {
             std::string{},
             std::string{"a\0b", 3},
             std::string{"/a"},
             std::string{"a/"},
             std::string{"a//b"},
             std::string{"."},
             std::string{".."},
             std::string{"a/./b"},
             std::string{"a/../b"},
             std::string{"a:b"},
             std::string{"a\\b"},
             std::string{"a\n//b"},
         }) {
        auto changed = encoded;
        changed["path"] = path;
        CHECK(!record::Codec::decode<WireStrings>(changed));
        WireStrings value;
        value.path = path;
        wire_refusal(record::Codec::encode(value));
    }
    for (const auto* const path : {"a/..\n", "a\n/b", "\n", "...", "a/.\nb"}) {
        WireStrings value;
        value.path = path;
        const auto healthy_3 = record::Codec::encode(value);
        REQUIRE(healthy_3);
        CHECK(record::Codec::decode<WireStrings>(*healthy_3));
    }
    WireStrings value;
    value.path.clear();
    for (unsigned i = 0; i < 64; ++i) {
        value.path += "\xc3\xa9";
    }
    const auto healthy_4 = record::Codec::encode(value);
    REQUIRE(healthy_4);
    CHECK(record::Codec::decode<WireStrings>(*healthy_4));
    value.path += "\xc3\xa9";
    auto changed = encoded;
    changed["path"] = value.path;
    CHECK(!record::Codec::decode<WireStrings>(changed));
    wire_refusal(record::Codec::encode(value));
}
TEST_CASE("String schemas state lexical calendar and character byte distinctions") {
    auto properties_result = record::Schema::of<WireStrings>();
    REQUIRE(properties_result);
    const auto properties = (*properties_result)["properties"];
    CHECK(properties["text"]["minLength"] == 1);
    CHECK(properties["text"]["pattern"] == R"(^[^\u0000]+$)");
    CHECK(properties["instant"]["minLength"] == 20);
    CHECK(properties["instant"]["maxLength"] == 20);
    CHECK(properties["instant"]["format"] == "date-time");
    REQUIRE(properties["instant"]["pattern"].is_string());
    CHECK(properties["instant"]["pattern"].get_ref<const std::string&>() == core::instant_pattern);
    CHECK(properties["path"]["minLength"] == 1);
    CHECK(properties["path"]["maxLength"] == 128);
    REQUIRE(properties["path"]["pattern"].is_string());
    CHECK(properties["path"]["pattern"].get_ref<const std::string&>() == core::bundle_path_pattern);
    CHECK(properties["literal"]["const"] == "agreement");
    const auto empty = record::Schema::of<WireEmptyLiteral>();
    REQUIRE(empty);
    const auto& empty_literal = (*empty)["properties"]["text"]["const"];
    REQUIRE(empty_literal.is_string());
    CHECK(empty_literal.get_ref<const std::string&>().empty());
    auto context_result = record::Schema::of<record::RunContext>();
    REQUIRE(context_result);
    const auto context = (*context_result)["properties"];
    CHECK(context["recorded"]["maxLength"] == 20);
    auto build_result = record::Schema::of<record::RecordedBuild>();
    REQUIRE(build_result);
    const auto build = (*build_result)["properties"];
    CHECK(build["application_version"]["minLength"] == 1);
    CHECK(!build["application_version"].contains("const"));
    const auto source = record::Schema::of<record::SourceFacts>();
    REQUIRE(source);
    CHECK((*source)["properties"]["name"]["minLength"] == 1);
}
} // namespace docenhance::tests
