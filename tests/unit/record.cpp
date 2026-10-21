// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/record/record.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/inventory.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/record_version.hpp"
#include "docenhance/record/request.hpp"
#include "png_depth_fixture.hpp"
#include "record_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace docenhance::tests {
namespace {
using record_fixture::binarized_record;
using record_fixture::other_digest;
using record_fixture::sample_digest;
std::span<const std::byte> as_bytes(std::string_view text) {
    // char is the narrow-character view of the same immutable bytes.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}
record::RunRecord restoration_record() {
    auto value = binarized_record();
    const auto method = methods::Wiener::create({.blend = 0});
    REQUIRE(method);
    const methods::Plan plan{
        .rotation = image::QuarterTurn(),
        .photometric =
            {
                .illumination = methods::IlluminationOff(),
                .denoising = methods::DenoisingOff(),
                .restoration = *method,
                .contrast = methods::ContrastOff(),
                .sharpening = methods::SharpenOff(),
            },
        .output = methods::ContinuousOutput(),
    };
    auto request = record::requested(plan, std::nullopt);
    REQUIRE(request);
    value.request = std::move(*request);
    value.output.depth = docenhance::tests::fixture_png_depth(8);
    value.output.profile_embedded = true;
    value.execution.interpretation.output = value.output.shape();
    value.execution.binarization.reset();
    value.execution.restoration = methods::WienerFacts{
        .reason = methods::WienerReason::zero_blend,
        .counts = {.eligible = 5000},
        .psf = {.width = 7, .height = 7, .normalized_sum = 1},
    };
    REQUIRE(record::validate(value));
    return value;
}
void refused_record(const record::Json& value) {
    const auto bytes = value.dump();
    const auto refused = record::read_record(as_bytes(bytes));
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::input);
}
} // namespace
TEST_CASE("A written record declares what the bundle contains", "[bundle]") {
    const auto written = record::serialize(binarized_record());
    REQUIRE(written);
    const auto declared = record::read_record(as_bytes(*written));
    REQUIRE(declared);
    CHECK(declared->context.version == record::record_version);
    CHECK(declared->context.identity == "0123456789abcdef0123456789abcdef");
    const auto inventory = record::inventory(*declared);
    REQUIRE(inventory);
    REQUIRE(inventory->size() == 1);
    CHECK(inventory->front().name == record::image_name);
    CHECK(inventory->front().identity.sha256 == other_digest);
    CHECK(inventory->front().identity.bytes == 2048);
    CHECK(!written->contains("\"publication\""));
    CHECK(!written->contains("\"record_sha256\""));
    CHECK(!written->contains("\"warnings\""));
    CHECK(written->contains("decoded_and_compared"));
    CHECK(declared->output.depth.bits() == 1);
}
TEST_CASE("A record that cannot be trusted is refused rather than read", "[bundle]") {
    const auto encoded = record::Codec::encode(binarized_record());
    REQUIRE(encoded);
    SECTION("malformed JSON") {
        CHECK(!record::read_record(as_bytes("{\"record\":")));
        CHECK(!record::read_record(as_bytes("")));
    }
    SECTION("a version this build does not support") {
        for (const auto version : {12, 14, 16}) {
            auto altered = *encoded;
            altered["record"]["version"] = version;
            refused_record(altered);
        }
    }
    SECTION("encoded output depth must belong to its actual container domain") {
        for (const auto depth : {0, 2, 4, 12, 32}) {
            auto altered = *encoded;
            altered["output"]["depth"] = depth;
            refused_record(altered);
        }
    }
    SECTION("a path that leaves the bundle") {
        for (const auto* const escape : {"../secret.png", "/etc/passwd", "a/../../b.png"}) {
            auto altered = *encoded;
            altered["output"]["path"] = escape;
            refused_record(altered);
        }
    }
    SECTION("a digest that is not one") {
        auto altered = *encoded;
        altered["output"]["sha256"] = std::string(64, 'z');
        refused_record(altered);
    }
    SECTION("nesting deep enough to exhaust a parser") {
        const std::string deep = std::string(record::record_max_depth + 4, '[') +
                                 std::string(record::record_max_depth + 4, ']');
        const auto refused = record::read_record(as_bytes(deep));
        REQUIRE(!refused);
        CHECK(refused.error().message.contains("nested"));
    }
    SECTION("more bytes than a record may have") {
        CHECK(!record::read_record(as_bytes(std::string(record::record_max_bytes + 1, ' '))));
    }
}
TEST_CASE("Record serialization never repairs malformed identity bytes", "[bundle][utf8]") {
    auto value = binarized_record();
    value.source.name = std::string(1, static_cast<char>(0xff));
    const auto rejected = record::serialize(value);
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
    const auto exact = record::read_record(as_bytes(array(record::record_max_events - 2)));
    REQUIRE(!exact);
    CHECK(exact.error().message == "Invalid typed wire value");
    const auto excess = record::read_record(as_bytes(array(record::record_max_events - 1)));
    REQUIRE(!excess);
    CHECK(excess.error().message.contains("parser event ceiling"));
    auto unread = array(record::record_max_events);
    unread.insert(unread.size() - 1, ",?");
    const auto stopped = record::read_record(as_bytes(unread));
    REQUIRE(!stopped);
    CHECK(stopped.error().code == core::ErrorCode::input);
    CHECK(stopped.error().message.contains("parser event ceiling"));
    auto value = binarized_record();
    value.source.name = "braces{}[]\\\".png";
    const auto written = record::serialize(value);
    REQUIRE(written);
    CHECK(record::read_record(as_bytes(*written)));
}
TEST_CASE("Record depth is bounded by library container events", "[bundle][resource]") {
    const auto nested = [](std::size_t depth) {
        std::string text = "0";
        for (std::size_t i = 0; i < depth; ++i) {
            text.insert(0, i % 2 == 0 ? "[" : "{\"x\":");
            text += i % 2 == 0 ? ']' : '}';
        }
        return text;
    };
    const auto exact = record::read_record(as_bytes(nested(record::record_max_depth)));
    REQUIRE(!exact);
    CHECK(exact.error().message == "Invalid typed wire value");
    const auto excessive = record::read_record(as_bytes(nested(record::record_max_depth + 1)));
    REQUIRE(!excessive);
    CHECK(excessive.error().message.contains("depth ceiling"));
    const auto unread =
        record::read_record(as_bytes(std::string(record::record_max_depth + 1, '[') + "?"));
    REQUIRE(!unread);
    CHECK(unread.error().message.contains("depth ceiling"));
    const auto duplicate = record::read_record(as_bytes(R"({"x":0,"\u0078":1})"));
    REQUIRE(!duplicate);
    CHECK(duplicate.error().message.contains("unique keys"));
    const auto quoted = record::read_record(as_bytes(R"({"x":"{}[]\\\""})"));
    REQUIRE(!quoted);
    CHECK(quoted.error().message == "Invalid typed wire value");
}
TEST_CASE("Otsu records retain the frozen fit and reject contradictory observations",
          "[bundle][otsu]") {
    auto value = binarized_record();
    const methods::Plan plan{
        .rotation = image::QuarterTurn(),
        .photometric = methods::Photometric(),
        .output = methods::BinaryOutput{.method = methods::Otsu::create()},
    };
    auto request = record::requested(plan, std::nullopt);
    REQUIRE(request);
    value.request = std::move(*request);
    methods::OtsuFacts facts;
    facts.white_samples = 5000;
    facts.single_bin_fallback = true;
    value.execution.binarization = facts;
    const auto written = record::serialize(value);
    REQUIRE(written);
    const auto declared = record::read_record(as_bytes(*written));
    REQUIRE(declared);
    CHECK(declared->execution.binarization == value.execution.binarization);
    const auto encoded = record::Codec::encode(value);
    REQUIRE(encoded);
    const auto sauvola = record::Codec::encode(binarized_record());
    REQUIRE(sauvola);
    for (unsigned change = 0; change < 5; ++change) {
        auto altered = *encoded;
        auto& observation = altered["execution"]["binarization"];
        if (change == 0) {
            observation = nullptr;
        } else if (change == 1) {
            observation["threshold_bin"] = 4095;
        } else if (change == 2) {
            observation["threshold_bin"] = 2046;
        } else if (change == 3) {
            altered["request"]["binarization"] = (*sauvola)["request"]["binarization"];
        } else {
            observation["white_samples"] = 4999;
        }
        refused_record(altered);
    }
}
TEST_CASE("Restoration records retain summaries and enforce the actual selected request",
          "[bundle][restoration]") {
    const auto value = restoration_record();
    const auto written = record::serialize(value);
    REQUIRE(written);
    const auto declared = record::read_record(as_bytes(*written));
    REQUIRE(declared);
    CHECK(declared->execution.restoration == value.execution.restoration);
    CHECK(!written->contains("coefficients"));
    const auto encoded = record::Codec::encode(value);
    REQUIRE(encoded);
    const auto motion = record::Codec::encode(methods::Psf{methods::MotionPsf{}});
    REQUIRE(motion);
    for (unsigned change = 0; change < 10; ++change) {
        auto altered = *encoded;
        auto& restoration = altered["execution"]["restoration"];
        if (change == 0) {
            restoration["psf"]["normalized_sum"] = 0.99;
        } else if (change == 1) {
            restoration["psf"]["centroid_x"] = 0.1;
        } else if (change == 2) {
            restoration["inference_warning"] = false;
        } else if (change == 3) {
            restoration["after_transform_warning"] = true;
        } else if (change == 4) {
            restoration["counts"]["eligible"] = 4999;
        } else if (change == 5) {
            restoration["transform"] = {
                {"guard", 32},
                {"width", 180},
                {"height", 120},
                {"padded_mean", 0.5},
            };
        } else if (change == 6) {
            restoration["native_calls"] = 1;
        } else if (change == 7) {
            altered["request"]["restoration"]["parameters"]["psf"] = *motion;
        } else if (change == 8) {
            restoration["psf"]["coefficients"] = record::Json::array({1});
        } else {
            altered["request"]["restoration"]["parameters"]["blend"] = 1;
        }
        refused_record(altered);
    }
}
TEST_CASE("Supplied PSF records bind confined assets without an external path",
          "[bundle][restoration]") {
    auto value = restoration_record();
    REQUIRE(value.request.restoration.has_value());
    if (!value.request.restoration.has_value()) {
        throw std::logic_error("Required restoration observation is absent");
    }
    REQUIRE(value.execution.restoration.has_value());
    if (!value.execution.restoration.has_value()) {
        throw std::logic_error("Required restoration observation is absent");
    }
    value.request.restoration.value().parameters.psf = methods::Kernel{};
    value.assets.emplace_back(
        record::PsfAsset{.sha256 = std::string{sample_digest}, .bytes = 1024});
    for (const auto side : {3U, methods::psf_maximum_side}) {
        value.execution.restoration.value().psf = {
            .kind = methods::PsfKind::kernel,
            .width = side,
            .height = side,
            .normalized_sum = 1,
        };
        const auto written = record::serialize(value);
        REQUIRE(written);
        CHECK(!written->contains("missing/kernel.png"));
        const auto declared = record::read_record(as_bytes(*written));
        REQUIRE(declared);
        if (!declared->execution.restoration.has_value()) {
            throw std::logic_error("Required decoded restoration observation is absent");
        }
        CHECK(declared->execution.restoration.value().psf ==
              value.execution.restoration.value().psf);
        const auto encoded = record::Codec::encode(value);
        REQUIRE(encoded);
        auto missing = *encoded;
        missing["assets"] = record::Json::array();
        refused_record(missing);
        auto external = *encoded;
        external["request"]["restoration"]["parameters"]["psf"]["path"] = "missing/kernel.png";
        refused_record(external);
    }
}
TEST_CASE("Restoration request parameters have one authority and are not verification proof",
          "[bundle][restoration]") {
    auto value = restoration_record();
    REQUIRE(value.request.restoration.has_value());
    if (!value.request.restoration.has_value()) {
        throw std::logic_error("Required restoration observation is absent");
    }
    value.request.restoration.value().parameters.k = 0.02;
    const auto written = record::serialize(value);
    REQUIRE(written);
    const auto declared = record::read_record(as_bytes(*written));
    REQUIRE(declared);
    REQUIRE(declared->request.restoration.has_value());
    if (!declared->request.restoration.has_value()) {
        throw std::logic_error("Required restoration observation is absent");
    }
    CHECK(declared->request.restoration.value().parameters.k == 0.02);
    // A bypass does not observe the regularizer. The persistent request carries its producer
    // claim once; the old duplicated execution parameter is now an unknown field.
    const auto encoded = record::Codec::encode(value);
    REQUIRE(encoded);
    auto duplicated = *encoded;
    duplicated["execution"]["restoration"]["k"] = 0.01;
    refused_record(duplicated);
}
} // namespace docenhance::tests
