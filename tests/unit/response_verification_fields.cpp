// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/response.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "docenhance/app/response_fields.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/record.hpp"
#include "wire_response_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
namespace docenhance::tests {
namespace {
std::pair<std::uint64_t, unsigned> confirmed_role(std::string_view path) {
    if (path == record::record_name) {
        return {record::record_max_bytes, 1};
    }
    if (path == record::image_name) {
        return {core::bundle_max_file_bytes, 0};
    }
    if (path == record::psf_name) {
        return {core::psf_encoded_bytes_max, 2};
    }
    return {core::bundle_max_file_bytes, 2};
}
} // namespace
namespace {
app::Outcome verified_response() {
    return {
        .command = app::Command::verify,
        .build = core::build_facts(),
        .payload =
            app::Verified{
                .directory = "bundle",
                .run = std::string(32, 'a'),
                .recorded = "2024-02-29T23:59:59Z",
                .confirmed =
                    {
                        {
                            .name = record::image_name,
                            .identity = {.sha256 = std::string(64, 'b'), .bytes = 1},
                        },
                        {
                            .name = record::record_name,
                            .identity = {.sha256 = std::string(64, 'c'), .bytes = 1},
                        },
                    },
            },
    };
}
} // namespace
TEST_CASE("Verified response requires unique observed result and record identities") {
    const auto encoded = wire(verified_response());
    REQUIRE(record::Codec::decode<app::WireResponse>(encoded));
    for (std::size_t index = 0; index < 2; ++index) {
        auto missing = encoded;
        missing["confirmed"].erase(index);
        CHECK(!record::Codec::decode<app::WireResponse>(missing));
        missing["confirmed"].push_back(encoded["confirmed"][0]);
        missing["confirmed"].back()["path"] = record::mask_name;
        CHECK(!record::Codec::decode<app::WireResponse>(missing));
        auto duplicate = encoded;
        duplicate["confirmed"].push_back(encoded["confirmed"][index]);
        CHECK(!record::Codec::decode<app::WireResponse>(duplicate));
    }
    for (const auto* const path : {"foreign.png", "a\n//b", "a/../b", "/a", "a/", "a/..\n"}) {
        auto changed = encoded;
        changed["confirmed"][0]["path"] = path;
        CHECK(!record::Codec::decode<app::WireResponse>(changed));
    }
    for (const auto* const field : {"path", "sha256", "bytes"}) {
        for (const auto& primitive : record::Json::array(
                 {nullptr, true, 1.5, record::Json::array(), record::Json::object()})) {
            auto changed = encoded;
            changed["confirmed"][0][field] = primitive;
            CHECK(!record::Codec::decode<app::WireResponse>(changed));
        }
    }
    auto changed = encoded;
    changed["confirmed"][0]["sha256"] = std::string(64, 'g');
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["recorded"] = "2023-02-29T23:59:59Z";
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
    changed = encoded;
    changed["establishes"] = "authentic";
    CHECK(!record::Codec::decode<app::WireResponse>(changed));
}
TEST_CASE("Verified receiving context retains exact distinct role byte boundaries") {
    const auto encoded = wire(verified_response());
    for (const auto* const path :
         {record::image_name, record::mask_name, record::psf_name, record::record_name}) {
        const auto [limit, index] = confirmed_role(path);
        auto healthy = encoded;
        if (index == 2) {
            healthy["confirmed"].push_back(encoded["confirmed"][0]);
        }
        healthy["confirmed"][index]["path"] = path;
        healthy["confirmed"][index]["bytes"] = limit;
        REQUIRE(record::Codec::decode<app::WireResponse>(healthy));
        for (const auto invalid : {std::uint64_t{0}, limit + 1}) {
            auto changed = healthy;
            changed["confirmed"][index]["bytes"] = invalid;
            CHECK(!record::Codec::decode<app::WireResponse>(changed));
        }
    }
    auto oversized = encoded;
    for (std::size_t i = 0; i < app::response_confirmed_limit; ++i) {
        oversized["confirmed"].push_back(encoded["confirmed"][0]);
    }
    CHECK(!record::Codec::decode<app::WireResponse>(oversized));
}
TEST_CASE("Confirmed scalar Fields retain the complete portable path byte domain") {
    const app::ResponseConfirmed value{
        .path = "a/..\n",
        .sha256 = std::string(64, 'b'),
        .bytes = 1,
    };
    const auto encoded = record::Codec::encode(value);
    REQUIRE(encoded);
    const auto decoded = record::Codec::decode<app::ResponseConfirmed>(*encoded);
    REQUIRE(decoded);
    CHECK(decoded->path == value.path);
    auto utf8 = value;
    utf8.path = "r\xc3\xa9pertoire/a/..\n";
    const auto utf8_encoded = record::Codec::encode(utf8);
    REQUIRE(utf8_encoded);
    const auto utf8_decoded = record::Codec::decode<app::ResponseConfirmed>(*utf8_encoded);
    REQUIRE(utf8_decoded);
    CHECK(utf8_decoded->path == utf8.path);
    auto longest = value;
    longest.path = std::string(128, 'a');
    REQUIRE(record::Codec::encode(longest));
    for (const auto& path : {std::string(129, 'a'), std::string{"a\0b", 3}, std::string{"\xff"}}) {
        auto malformed = *encoded;
        malformed["path"] = path;
        CHECK(!record::Codec::decode<app::ResponseConfirmed>(malformed));
    }
    for (const auto* const path : {"a\n//b", "a/../b", "/a", "a/"}) {
        auto changed = *encoded;
        changed["path"] = path;
        CHECK(!record::Codec::decode<app::ResponseConfirmed>(changed));
    }
}
} // namespace docenhance::tests
