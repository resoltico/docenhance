// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/bundle/record.hpp"

#include "docenhance/bundle/inventory.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binarization.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace docenhance::tests {
namespace {
constexpr std::string_view sample_digest =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr std::string_view other_digest =
    "3f79bb7b435b05321651daefd374cdc681dc06faa65e374e38337b88ca046dea";

std::span<const std::byte> as_bytes(std::string_view text) {
    // char is the narrow-character view of the same immutable bytes.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

bundle::RunRecord binarized_record() {
    const auto method = methods::Sauvola::create();
    REQUIRE(method);
    return {
        .context =
            {
                .identity = "0123456789abcdef0123456789abcdef",
                .recorded = "2026-09-28T00:00:00Z",
            },
        .build =
            {
                .version = "0.3.0",
                .platform = "Darwin-arm64",
                .compiler = "AppleClang 21",
                .dependency_lock_sha256 = other_digest,
            },
        .source =
            {
                .identity = {.sha256 = std::string(sample_digest), .bytes = 1024},
                .name = "page.png",
            },
        .operation = methods::Binarization{*method},
        .protection_supplied = false,
        .output =
            {
                .artifact =
                    {
                        .path = bundle::image_name,
                        .identity = {.sha256 = std::string(other_digest), .bytes = 2048},
                    },
                .shape =
                    {
                        .width = 100,
                        .height = 50,
                        .model = image::SampleModel::gray,
                        .depth = 8,
                    },
                .profile_embedded = false,
                .resolution = std::nullopt,
                .verification = bundle::Verification::decoded_and_compared,
            },
    };
}
} // namespace

TEST_CASE("A written record declares what the bundle contains", "[bundle]") {
    const auto written = bundle::serialize(binarized_record());
    REQUIRE(written);
    const auto declared = bundle::read_record(as_bytes(*written));
    REQUIRE(declared);
    CHECK(declared->version == bundle::record_version);
    CHECK(declared->run == "0123456789abcdef0123456789abcdef");
    REQUIRE(declared->inventory.size() == 1);
    CHECK(declared->inventory.front().path == bundle::image_name);
    CHECK(declared->inventory.front().identity.sha256 == std::string(other_digest));
    CHECK(declared->inventory.front().identity.bytes == 2048);

    // The record describes processing. Publication happens after it is written, so it may not
    // claim that anything was published, and it never carries its own digest.
    CHECK(!written->contains("\"publication\""));
    CHECK(!written->contains("\"record_sha256\""));
    // What was verified is named, not asserted as a bare truth.
    CHECK(written->contains("decoded_and_compared"));
    CHECK(written->contains("integer_samples"));
}

TEST_CASE("A record that cannot be trusted is refused rather than read", "[bundle]") {
    const auto written = bundle::serialize(binarized_record());
    REQUIRE(written);

    SECTION("malformed JSON") {
        CHECK(!bundle::read_record(as_bytes("{\"record\":")));
        CHECK(!bundle::read_record(as_bytes("")));
    }
    SECTION("a version this build does not support") {
        auto altered = *written;
        const auto at = altered.find("\"version\": 1");
        REQUIRE(at != std::string::npos);
        altered.replace(at, std::string_view("\"version\": 1").size(), "\"version\": 2");
        const auto refused = bundle::read_record(as_bytes(altered));
        REQUIRE(!refused);
        CHECK(refused.error().code == core::ErrorCode::input);
    }
    SECTION("a path that leaves the bundle") {
        for (const auto* const escape : {"../secret.png", "/etc/passwd", "a/../../b.png"}) {
            auto altered = *written;
            const auto at = altered.find(bundle::image_name);
            REQUIRE(at != std::string::npos);
            altered.replace(at, std::string_view(bundle::image_name).size(), escape);
            CHECK(!bundle::read_record(as_bytes(altered)));
        }
    }
    SECTION("a digest that is not one") {
        auto altered = *written;
        // The output's digest, not the build's: only the artifacts are ones a bundle can check.
        const auto output_at = altered.find("\"output\"");
        REQUIRE(output_at != std::string::npos);
        const auto at = altered.find(other_digest, output_at);
        REQUIRE(at != std::string::npos);
        altered.replace(at, other_digest.size(), std::string(other_digest.size(), 'z'));
        CHECK(!bundle::read_record(as_bytes(altered)));
    }
    SECTION("nesting deep enough to exhaust a parser") {
        const std::string deep = std::string(bundle::record_max_depth + 4, '[') +
                                 std::string(bundle::record_max_depth + 4, ']');
        const auto refused = bundle::read_record(as_bytes(deep));
        REQUIRE(!refused);
        CHECK(refused.error().message.contains("nested"));
    }
    SECTION("more bytes than a record may have") {
        const std::string large(bundle::record_max_bytes + 1, ' ');
        CHECK(!bundle::read_record(as_bytes(large)));
    }
}
} // namespace docenhance::tests
