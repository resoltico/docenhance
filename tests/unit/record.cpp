// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/record/record.hpp"

#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/otsu.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/progress.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/record/inventory.hpp"
#include "docenhance/record/record_version.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

record::RunRecord binarized_record() {
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
                    image::PngFacts{.width = 100, .height = 50, .depth = 8, .color_type = 0},
            },
        .operation = methods::Binarization{*method},
        .protection_supplied = false,
        .output =
            {
                .artifact =
                    {
                        .name = record::image_name,
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
                .verification = record::Verification::decoded_and_compared,
            },
        // A binarized run with no mask and no continuous conversion says so, rather than
        // leaving the record's optional sections to whatever a default would be.
        .protection = std::nullopt,
        .conversion = std::nullopt,
        .stages =
            {
                .illumination = illumination,
                .denoising = {.progress = methods::StageProgress::completed},
                .restoration = {.progress = methods::StageProgress::completed},
                .contrast = {.progress = methods::StageProgress::completed},
                .sharpening = {.progress = methods::StageProgress::completed},
            },
    };
}
} // namespace

TEST_CASE("A written record declares what the bundle contains", "[bundle]") {
    const auto written = record::serialize(binarized_record());
    REQUIRE(written);
    const auto declared = record::read_record(as_bytes(*written));
    REQUIRE(declared);
    CHECK(declared->version == record::record_version);
    CHECK(declared->run == "0123456789abcdef0123456789abcdef");
    REQUIRE(declared->inventory.size() == 1);
    CHECK(declared->inventory.front().name == record::image_name);
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
    const auto written = record::serialize(binarized_record());
    REQUIRE(written);

    SECTION("malformed JSON") {
        CHECK(!record::read_record(as_bytes("{\"record\":")));
        CHECK(!record::read_record(as_bytes("")));
    }
    SECTION("a version this build does not support") {
        auto altered = *written;
        const auto at = altered.find("\"version\": 14");
        REQUIRE(at != std::string::npos);
        altered.replace(at, std::string_view("\"version\": 14").size(), "\"version\": 12");
        const auto refused = record::read_record(as_bytes(altered));
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
            const auto refused = record::read_record(as_bytes(altered));
            REQUIRE(!refused);
            CHECK(refused.error().code == core::ErrorCode::input);
        }
    }
    SECTION("a path that leaves the bundle") {
        for (const auto* const escape : {"../secret.png", "/etc/passwd", "a/../../b.png"}) {
            auto altered = *written;
            const auto at = altered.find(record::image_name);
            REQUIRE(at != std::string::npos);
            altered.replace(at, std::string_view(record::image_name).size(), escape);
            CHECK(!record::read_record(as_bytes(altered)));
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
        CHECK(!record::read_record(as_bytes(altered)));
    }
    SECTION("nesting deep enough to exhaust a parser") {
        const std::string deep = std::string(record::record_max_depth + 4, '[') +
                                 std::string(record::record_max_depth + 4, ']');
        const auto refused = record::read_record(as_bytes(deep));
        REQUIRE(!refused);
        CHECK(refused.error().message.contains("nested"));
    }
    SECTION("more bytes than a record may have") {
        const std::string large(record::record_max_bytes + 1, ' ');
        CHECK(!record::read_record(as_bytes(large)));
    }
}
TEST_CASE("Record serialization never repairs malformed identity bytes", "[bundle][utf8]") {
    auto record = binarized_record();
    record.source.name = std::string(1, static_cast<char>(0xff));
    const auto rejected = record::serialize(record);
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
    CHECK(exact.error().message.contains("identifying header"));
    const auto excess = record::read_record(as_bytes(array(record::record_max_events - 1)));
    REQUIRE(!excess);
    CHECK(excess.error().message.contains("parser event ceiling"));
    auto unread = array(record::record_max_events);
    unread.insert(unread.size() - 1, ",?");
    const auto stopped = record::read_record(as_bytes(unread));
    REQUIRE(!stopped);
    CHECK(stopped.error().code == core::ErrorCode::input);
    CHECK(stopped.error().message.contains("parser event ceiling"));

    auto record = binarized_record();
    record.source.name = "braces{}[]\\\".png";
    const auto written = record::serialize(record);
    REQUIRE(written);
    CHECK(record::read_record(as_bytes(*written)));
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
    const auto exact = record::read_record(as_bytes(nested(record::record_max_depth)));
    REQUIRE(!exact);
    CHECK(exact.error().message.contains("identifying header"));
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
    CHECK(quoted.error().message.contains("identifying header"));
}
TEST_CASE("Otsu records retain the frozen fit and reject contradictory observations",
          "[bundle][otsu]") {
    auto facts = binarized_record();
    facts.operation = methods::Binarization{methods::Otsu::create()};
    facts.otsu = methods::OtsuObservation{.threshold_bin = 2047, .single_bin_fallback = true};
    const auto written = record::serialize(facts);
    REQUIRE(written);
    const auto declared = record::read_record(as_bytes(*written));
    REQUIRE(declared);
    CHECK(declared->otsu == facts.otsu);
    for (unsigned change = 0; change < 4; ++change) {
        auto altered = facts;
        if (change == 0) {
            altered.otsu.reset();
        } else if (change == 1) {
            altered.otsu->threshold_bin = 4095;
        } else if (change == 2) {
            altered.otsu->threshold_bin = 2046;
        } else {
            altered.operation = binarized_record().operation;
        }
        const auto invalid = record::serialize(altered);
        REQUIRE(invalid);
        CHECK(!record::read_record(as_bytes(*invalid)));
    }
}
namespace {
record::RunRecord restoration_record() {
    auto record = binarized_record();
    record.operation = image::Continuous::create({}).value();
    record.output.profile_embedded = true;
    record.conversion = image::ConversionReport{
        .source = record.output.shape,
        .output = record.output.shape,
        .interpretation = image::Interpretation::assumed_srgb,
        .resolution = std::nullopt,
        .assumed_transfer = true,
        .verified = true,
    };
    record.stages.illumination.progress = methods::StageProgress::completed;
    record.stages.illumination.eligible_samples = 5000;
    record.stages.denoising.eligible_samples = 5000;
    record.stages.contrast.eligible_samples = 5000;
    record.stages.sharpening.eligible_samples = 5000;
    const auto method = methods::Wiener::create({.blend = 0}).value();
    methods::ResolvedPsf psf{.width = 7, .height = 7, .coefficients = {}};
    psf.coefficients.resize(49);
    REQUIRE(methods::gaussian_psf({}, psf.coefficients));
    record.stages.restoration = {
        .status = methods::RestorationStatus::no_change,
        .reason = methods::RestorationReason::zero_blend,
        .progress = methods::StageProgress::completed,
        .requested = method.parameters(),
        .psf = std::move(psf),
        .eligible_samples = 5000,
        .inference_warning = true,
    };
    return record;
}
} // namespace
TEST_CASE("Restoration records retain resolved kernels and enforce request agreement",
          "[bundle][restoration]") {
    auto record = restoration_record();
    const auto written = record::serialize(record);
    REQUIRE(written);
    auto declared = record::read_record(as_bytes(*written));
    REQUIRE(declared);
    CHECK(declared->restoration == record.stages.restoration);
    for (unsigned change = 0; change < 8; ++change) {
        auto altered = record;
        auto& restoration = altered.stages.restoration;
        if (!restoration.psf || !restoration.requested) {
            FAIL("Restoration fixture requires resolved PSF and request parameters");
            return;
        }
        if (change == 0) {
            restoration.psf.value().coefficients.at(0) += 0.01;
            restoration.psf.value().coefficients.at(1) -= 0.01;
        } else if (change == 1) {
            restoration.psf.value().centroid_x = 0.1;
        } else if (change == 2) {
            restoration.inference_warning = false;
        } else if (change == 3) {
            restoration.after_transform_warning = true;
        } else if (change == 4) {
            restoration.eligible_samples -= 1;
        } else if (change == 5) {
            restoration.guard = 32;
        } else if (change == 6) {
            restoration.native_calls = 1;
        } else {
            restoration.requested.value().psf = methods::MotionPsf{};
        }
        const auto invalid = record::serialize(altered);
        REQUIRE(invalid);
        CHECK(!record::read_record(as_bytes(*invalid)));
    }
    auto mismatch = *written;
    const auto execution = mismatch.find("\"execution\"");
    REQUIRE(execution != std::string::npos);
    const auto at = mismatch.find("\"k\": 0.01", execution);
    REQUIRE(at != std::string::npos);
    mismatch.replace(at, std::string_view("\"k\": 0.01").size(), "\"k\": 0.02");
    CHECK(!record::read_record(as_bytes(mismatch)));
}
TEST_CASE("Supplied PSF record verification does not depend on a present external path",
          "[bundle][restoration]") {
    auto record = restoration_record();
    auto& restoration = record.stages.restoration;
    if (!restoration.requested) {
        FAIL("Restoration fixture requires request parameters");
        return;
    }
    restoration.requested.value().psf = methods::FilePsf{.path = "missing/kernel.png"};
    for (const auto side : {std::size_t{3}, core::psf_dimension_max}) {
        restoration.psf = methods::ResolvedPsf{
            .width = static_cast<std::uint32_t>(side),
            .height = static_cast<std::uint32_t>(side),
            .coefficients = std::vector<double>(side * side, 0),
            .source_identity =
                methods::PsfIdentity{
                    .path = "missing/kernel.png",
                    .identity = {.sha256 = std::string(sample_digest), .bytes = 1024},
                },
        };
        restoration.psf->coefficients.at((side * side) / 2) = 1;
        const auto written = record::serialize(record);
        REQUIRE(written);
        const auto declared = record::read_record(as_bytes(*written));
        REQUIRE(declared);
        CHECK(declared->restoration.psf == restoration.psf);
        restoration.psf.value().source_identity.reset();
        const auto invalid = record::serialize(record);
        REQUIRE(invalid);
        CHECK(!record::read_record(as_bytes(*invalid)));
    }
}
} // namespace docenhance::tests
