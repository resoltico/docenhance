// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/bundle/record.hpp"

#include "docenhance/bundle/inventory.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/illumination.hpp"

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
    const methods::IlluminationReport illumination;
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
                .decoding =
                    image::PngSource{.width = 100, .height = 50, .depth = 8, .color_type = 0},
            },
        .operation = methods::Binarization{*method},
        .protection_supplied = false,
        .output =
            {
                .artifact =
                    {
                        .name = bundle::image_name,
                        .identity = {.sha256 = std::string(other_digest), .bytes = 2048},
                    },
                .shape =
                    {
                        .width = 100,
                        .height = 50,
                        .model = image::SampleModel::gray,
                        .depth = image::SampleDepth::byte(),
                    },
                .profile_embedded = false,
                .resolution = std::nullopt,
                .verification = bundle::Verification::decoded_and_compared,
            },
        // A binarized run with no mask and no continuous conversion says so, rather than
        // leaving the record's optional sections to whatever a default would be.
        .protection = std::nullopt,
        .conversion = std::nullopt,
        .illumination = illumination,
        .denoising = {.complete = true},
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
    CHECK(declared->inventory.front().name == bundle::image_name);
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
        const auto at = altered.find("\"version\": 5");
        REQUIRE(at != std::string::npos);
        altered.replace(at, std::string_view("\"version\": 5").size(), "\"version\": 4");
        const auto refused = bundle::read_record(as_bytes(altered));
        REQUIRE(!refused);
        CHECK(refused.error().code == core::ErrorCode::input);
    }
    SECTION("decoded output depth must belong to the admitted domain") {
        for (const auto depth : {0, 1, 12, 32}) {
            auto altered = *written;
            const auto output_at = altered.find("\"output\"");
            REQUIRE(output_at != std::string::npos);
            const auto at = altered.find("\"bit_depth\": 8", output_at);
            REQUIRE(at != std::string::npos);
            altered.replace(at, std::string_view("\"bit_depth\": 8").size(),
                            "\"bit_depth\": " + std::to_string(depth));
            const auto refused = bundle::read_record(as_bytes(altered));
            REQUIRE(!refused);
            CHECK(refused.error().code == core::ErrorCode::input);
        }
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
TEST_CASE("Record serialization never repairs malformed identity bytes", "[bundle][utf8]") {
    auto record = binarized_record();
    record.source.name = std::string(1, static_cast<char>(0xff));
    const auto rejected = bundle::serialize(record);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == core::ErrorCode::invariant);
}
TEST_CASE("Record parsing stops at its event boundary before later malformed tokens",
          "[bundle][resource]") {
    const auto array = [](std::size_t values) {
        std::string text = "[";
        for (std::size_t i = 0; i < values; ++i) {
            text += i == 0 ? "0" : ",0";
        }
        return text + "]";
    };
    const auto exact = bundle::read_record(as_bytes(array(bundle::record_max_events - 2)));
    REQUIRE(!exact);
    CHECK(exact.error().message.contains("identifying header"));
    const auto excess = bundle::read_record(as_bytes(array(bundle::record_max_events - 1)));
    REQUIRE(!excess);
    CHECK(excess.error().message.contains("parser event ceiling"));
    auto unread = array(bundle::record_max_events);
    unread.insert(unread.size() - 1, ",?");
    const auto stopped = bundle::read_record(as_bytes(unread));
    REQUIRE(!stopped);
    CHECK(stopped.error().code == core::ErrorCode::input);
    CHECK(stopped.error().message.contains("parser event ceiling"));

    auto record = binarized_record();
    record.source.name = "braces{}[]\\\".png";
    const auto written = bundle::serialize(record);
    REQUIRE(written);
    CHECK(bundle::read_record(as_bytes(*written)));
}
} // namespace docenhance::tests

namespace docenhance::tests {
TEST_CASE("Record depth is bounded by library container events", "[bundle][resource]") {
    const auto nested = [](std::size_t depth) {
        std::string text = "0";
        for (std::size_t i = 0; i < depth; ++i) {
            text.insert(0, i % 2 == 0 ? "[" : "{\"x\":");
            text += i % 2 == 0 ? ']' : '}';
        }
        return text;
    };
    const auto exact = bundle::read_record(as_bytes(nested(bundle::record_max_depth)));
    REQUIRE(!exact);
    CHECK(exact.error().message.contains("identifying header"));
    const auto excessive = bundle::read_record(as_bytes(nested(bundle::record_max_depth + 1)));
    REQUIRE(!excessive);
    CHECK(excessive.error().message.contains("depth ceiling"));
    const auto unread =
        bundle::read_record(as_bytes(std::string(bundle::record_max_depth + 1, '[') + "?"));
    REQUIRE(!unread);
    CHECK(unread.error().message.contains("depth ceiling"));
    const auto duplicate = bundle::read_record(as_bytes(R"({"x":0,"\u0078":1})"));
    REQUIRE(!duplicate);
    CHECK(duplicate.error().message.contains("unique keys"));
    const auto quoted = bundle::read_record(as_bytes(R"({"x":"{}[]\\\""})"));
    REQUIRE(!quoted);
    CHECK(quoted.error().message.contains("identifying header"));
}
} // namespace docenhance::tests
