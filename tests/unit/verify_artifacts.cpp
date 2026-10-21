// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/host/processor.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/record/inventory.hpp"
#include "docenhance/record/record.hpp"
#include "file_contents.hpp"
#include "png_fixture.hpp"
#include "temporary_directory.hpp"
#include "verify_fixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
namespace docenhance::tests {
namespace {
record::RunRecord declared_record(const std::filesystem::path& bundle) {
    const auto bytes = file_contents(bundle / record::record_name);
    auto declared = record::read_record(std::as_bytes(std::span{bytes}));
    REQUIRE(declared);
    return std::move(*declared);
}
void write_record(const std::filesystem::path& bundle, const record::RunRecord& value) {
    REQUIRE(record::validate(value));
    const auto written = record::serialize(value);
    REQUIRE(written);
    std::ofstream output{bundle / record::record_name, std::ios::binary};
    output.write(written->data(), static_cast<std::streamsize>(written->size()));
    output.flush();
    REQUIRE(output.good());
}
void replace_asset_identity(record::RunRecord& declared, const std::string& name,
                            const core::ContentIdentity& identity) {
    bool found = false;
    for (auto& asset : declared.assets) {
        std::visit(
            [&](auto& value) {
                const bool mask = std::holds_alternative<record::ProtectionAsset>(asset);
                if ((mask && name == record::mask_name) || (!mask && name == record::psf_name)) {
                    value.sha256 = identity.sha256;
                    value.bytes = identity.bytes;
                    found = true;
                }
            },
            asset);
    }
    REQUIRE(found);
}
void replace_png(const std::filesystem::path& bundle, const std::string& name,
                 const std::vector<std::uint8_t>& encoded, record::RunRecord& declared) {
    write_fixture_png(bundle / name, encoded);
    const auto identity = fs::identify(std::as_bytes(std::span{encoded}));
    REQUIRE(identity);
    if (name == record::image_name) {
        declared.output.sha256 = identity->sha256;
        declared.output.bytes = identity->bytes;
    } else {
        replace_asset_identity(declared, name, *identity);
    }
    write_record(bundle, declared);
}
void verify_input_refusal(const std::filesystem::path& bundle) {
    const auto refused = read_back(bundle);
    REQUIRE(!refused);
    CHECK(refused.error().code == core::ErrorCode::input);
}
} // namespace
TEST_CASE("Verification reconciles actual logical counts after valid record edits", "[verify]") {
    const TemporaryDirectory temporary{"docenhance-verify-counts"};
    host::Processor processor{pinned_context};
    const auto bundle = published_bundle(temporary, "run", processor);
    REQUIRE(read_back(bundle));
    auto declared = declared_record(bundle);
    REQUIRE(declared.execution.binarization.has_value());
    if (!declared.execution.binarization.has_value()) {
        throw std::logic_error("Required test observation is absent");
    }
    auto& counts = std::get<methods::FixedFacts>(declared.execution.binarization.value());
    REQUIRE(counts.black_samples == 0);
    REQUIRE(counts.white_samples == 32);
    counts.black_samples = 1;
    counts.white_samples = 31;
    write_record(bundle, declared);
    verify_input_refusal(bundle);
}
TEST_CASE("Canonical binary tails are checked after CRC digest and size agreement", "[verify]") {
    const TemporaryDirectory temporary{"docenhance-verify-binary-tail"};
    host::Processor processor{pinned_context};
    const auto bundle = published_bundle(temporary, "run", processor, {.width = 7});
    auto declared = declared_record(bundle);
    GrayFixture fixture{
        .width = 7,
        .height = 4,
        .depth = 1,
        .interlaced = false,
        .samples = std::vector<std::uint8_t>(28, 1),
    };
    replace_png(bundle, record::image_name, make_gray_png(fixture), declared);
    REQUIRE(read_back(bundle));
    // The independent fixture regenerates zlib checksums and PNG CRCs for the changed raw tail.
    fixture.unused_tail = 1;
    replace_png(bundle, record::image_name, make_gray_png(fixture), declared);
    verify_input_refusal(bundle);
}
TEST_CASE("Confined mask and PSF observations remain authoritative after digest repair",
          "[verify]") {
    const TemporaryDirectory temporary{"docenhance-verify-assets"};
    host::Processor processor{pinned_context};
    const auto bundle = published_bundle(temporary, "run", processor, {.assets = true});
    std::filesystem::remove(temporary.path / "run-page.png");
    std::filesystem::remove(temporary.path / "run-mask.png");
    std::filesystem::remove(temporary.path / "run-psf.png");
    const auto expected = verified_expected_files(
        bundle, {record::image_name, record::mask_name, record::psf_name, record::record_name});
    const auto confirmed = read_back(bundle);
    REQUIRE(confirmed);
    check_verified_files(*confirmed, expected);
    auto declared = declared_record(bundle);
    SECTION("a normalized supplied PSF with a different centroid") {
        const auto shifted = make_gray_png({
            .width = 3,
            .height = 3,
            .depth = 8,
            .interlaced = false,
            .samples = {0, 0, 0, 0, 0, 255, 0, 0, 0},
        });
        replace_png(bundle, record::psf_name, shifted, declared);
        verify_input_refusal(bundle);
    }
    SECTION("a mask sample outside its canonical logical domain") {
        auto samples = std::vector<std::uint8_t>(32, 0);
        samples.front() = 2;
        replace_png(bundle, record::mask_name,
                    make_gray_png({
                        .width = 8,
                        .height = 4,
                        .depth = 8,
                        .interlaced = false,
                        .samples = std::move(samples),
                    }),
                    declared);
        verify_input_refusal(bundle);
    }
    SECTION("a missing confined PSF") {
        std::filesystem::remove(bundle / record::psf_name);
        verify_input_refusal(bundle);
    }
    SECTION("a foreign assets file") {
        std::ofstream{bundle / "assets" / "foreign.txt"}.put('x');
        verify_input_refusal(bundle);
    }
}
TEST_CASE("Verification is read-only agreement and does not authenticate producer claims",
          "[verify]") {
    const TemporaryDirectory temporary{"docenhance-verify-producer-claims"};
    host::Processor processor{pinned_context};
    const auto bundle = published_bundle(temporary, "run", processor, {.binary = false});
    auto declared = declared_record(bundle);
    REQUIRE(declared.output.profile_embedded);
    declared.source.sha256 = std::string(64, 'e');
    declared.build.compiler = "Claimed compiler; verification cannot establish this event";
    write_record(bundle, declared);
    const auto image_before = file_contents(bundle / record::image_name);
    const auto record_before = file_contents(bundle / record::record_name);
    REQUIRE(read_back(bundle));
    REQUIRE(read_back(bundle));
    CHECK(file_contents(bundle / record::image_name) == image_before);
    CHECK(file_contents(bundle / record::record_name) == record_before);
    CHECK(!std::filesystem::exists(temporary.path / "run.staging-0"));
    CHECK(!std::filesystem::exists(bundle / "assets"));
}
} // namespace docenhance::tests
