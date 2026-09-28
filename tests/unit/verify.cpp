// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/app/verify.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/host/processor.hpp"
#include "docenhance/host/verifier.hpp"
#include "png_fixture.hpp"
#include "temporary_directory.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

namespace docenhance::tests {
namespace {
// A published bundle to read back, produced by the real processing path.
std::filesystem::path published_bundle(const TemporaryDirectory& temporary,
                                       const std::string& name) {
    const auto input = temporary.path / (name + "-page.png");
    const GrayFixture fixture{
        .width = 8,
        .height = 4,
        .depth = 8,
        .interlaced = false,
        .samples = std::vector<std::uint8_t>(std::size_t{8} * 4, 128),
    };
    const auto encoded = make_gray_png(fixture);
    {
        std::ofstream writing{input, std::ios::binary};
        writing.write(reinterpret_cast<const char*>(encoded.data()), // NOLINT(*-reinterpret-cast)
                      static_cast<std::streamsize>(encoded.size()));
    }
    contract::Invocation invocation;
    invocation.command = contract::Command::process;
    invocation.subject = utf8_spelling(input);
    invocation.output_directory = utf8_spelling(temporary.path / name);
    invocation.output_mode = "bw";
    invocation.binarize = "fixed";
    invocation.fixed_threshold = "0.5";
    auto request = app::prepare_process(invocation);
    REQUIRE(request);
    host::Processor processor{
        {.identity = std::string(32, 'a'), .recorded = "2026-01-01T00:00:00Z"}};
    const auto published = processor.process(*request, {});
    REQUIRE(published);
    return temporary.path / name;
}

core::Result<app::Verified> read_back(const std::filesystem::path& directory) {
    contract::Invocation invocation;
    invocation.command = contract::Command::verify;
    invocation.subject = utf8_spelling(directory);
    auto request = app::prepare_verify(invocation);
    REQUIRE(request);
    host::Verifier verifier;
    return verifier.verify(*request, {});
}
} // namespace

TEST_CASE("A bundle agrees with the record it was published with", "[verify]") {
    const TemporaryDirectory temporary{"docenhance-verify"};
    const auto bundle = published_bundle(temporary, "run");
    const auto confirmed = read_back(bundle);
    REQUIRE(confirmed);
    CHECK(confirmed->run == std::string(32, 'a'));
    CHECK(confirmed->recorded == "2026-01-01T00:00:00Z");
    REQUIRE(confirmed->confirmed.size() == 1);
    CHECK(confirmed->confirmed.front().name == std::string(bundle::image_name));

    // A bundle is read where it is, not where it was written: every declared path is relative.
    const auto moved = temporary.path / "moved";
    std::filesystem::rename(bundle, moved);
    CHECK(read_back(moved));
}

TEST_CASE("A bundle that disagrees with its record is refused", "[verify]") {
    const TemporaryDirectory temporary{"docenhance-verify-refusal"};

    SECTION("an artifact that is not the one recorded") {
        const auto bundle = published_bundle(temporary, "tampered");
        {
            std::ofstream appending{bundle / bundle::image_name, std::ios::binary | std::ios::app};
            appending.put('\0');
        }
        const auto refused = read_back(bundle);
        REQUIRE(!refused);
        CHECK(refused.error().code == core::ErrorCode::input);
    }
    SECTION("a declared artifact that is absent") {
        const auto bundle = published_bundle(temporary, "missing");
        std::filesystem::remove(bundle / bundle::image_name);
        CHECK(!read_back(bundle));
    }
    SECTION("a file the record does not declare") {
        const auto bundle = published_bundle(temporary, "extra");
        std::ofstream{bundle / "notes.txt", std::ios::binary}.put('x');
        CHECK(!read_back(bundle));
    }
    SECTION("a record that is not there at all") {
        const auto bundle = published_bundle(temporary, "recordless");
        std::filesystem::remove(bundle / bundle::record_name);
        CHECK(!read_back(bundle));
    }
    SECTION("a record this build cannot read") {
        const auto bundle = published_bundle(temporary, "malformed");
        std::ofstream{bundle / bundle::record_name, std::ios::binary | std::ios::trunc} << "{";
        CHECK(!read_back(bundle));
    }
    SECTION("a record past the bound it is read under") {
        const auto bundle = published_bundle(temporary, "oversized");
        std::ofstream padding{bundle / bundle::record_name, std::ios::binary | std::ios::trunc};
        padding << std::string(bundle::record_max_bytes + 1, ' ');
        padding.close();
        CHECK(!read_back(bundle));
    }
}
} // namespace docenhance::tests
