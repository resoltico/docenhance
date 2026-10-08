// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "bundle_verify.hpp"
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/artifact_limits.hpp"
#include "docenhance/io/digest.hpp"
#include "docenhance/io/publication.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/illumination.hpp"
#include "file_contents.hpp"
#include "native_publication.hpp"
#include "png_fixture.hpp"
#include "temporary_directory.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>
namespace docenhance::tests {
namespace {
std::span<const std::byte> bytes(const std::string& value) {
    return std::as_bytes(std::span{value.data(), value.size()});
}
struct WrittenBundle {
    std::string image;
    std::string record;
    host::ExpectedBundle expected;
};
WrittenBundle written_bundle() {
    const auto png = make_gray_png({
        .width = 2,
        .height = 2,
        .depth = 8,
        .interlaced = false,
        .samples = std::vector<std::uint8_t>(4, 0),
    });
    std::string image;
    for (const auto value : png) {
        image.push_back(static_cast<char>(value));
    }
    const auto digest = io::identify(bytes(image));
    REQUIRE(digest);
    const auto method = methods::FixedThreshold::create();
    REQUIRE(method);
    const methods::IlluminationReport illumination;
    const bundle::RunRecord facts{
        .context = {.identity = std::string(32, 'a'), .recorded = "2026-09-30T00:00:00Z"},
        .build = core::build_facts(),
        .source =
            {
                .identity = {.sha256 = std::string(64, 'b'), .bytes = 1},
                .name = "source.png",
                .decoding = image::PngSource{.width = 2, .height = 2, .depth = 8, .color_type = 0},
            },
        .operation = methods::Binarization{*method},
        .protection_supplied = false,
        .output =
            {
                .artifact = {.name = bundle::image_name, .identity = *digest},
                .shape =
                    {
                        .width = 2,
                        .height = 2,
                        .model = image::SampleModel::gray,
                        .depth = image::SampleDepth::byte(),
                    },
                .profile_embedded = false,
                .resolution = std::nullopt,
                .verification = bundle::Verification::decoded_and_compared,
            },
        .protection = std::nullopt,
        .conversion = std::nullopt,
        .illumination = illumination,
        .denoising = {.complete = true},
        .contrast = {.complete = true},
        .sharpening = {.complete = true},
        .restoration = {.complete = true},
    };
    auto record = bundle::serialize(facts);
    REQUIRE(record);
    const auto identity = io::identify(bytes(*record));
    REQUIRE(identity);
    return {
        .image = image,
        .record = *record,
        .expected = {.run = facts.context.identity, .record = *identity},
    };
}
core::Result<void> write_text(void* const state, const io::BundleSlot& slot) {
    return io::write_bytes(slot, *static_cast<std::string*>(state));
}
std::error_code lost_reply(const std::filesystem::path& from,
                           const std::filesystem::path& to) noexcept {
    const auto actual = io::rename_exclusive(from, to);
    return actual ? actual : std::make_error_code(std::errc::io_error);
}
std::error_code ambiguous_refusal(const std::filesystem::path& /*unused*/,
                                  const std::filesystem::path& /*unused*/) noexcept {
    return std::make_error_code(std::errc::io_error);
}
std::error_code corrupt_after_commit(const std::filesystem::path& from,
                                     const std::filesystem::path& to) noexcept {
    const auto actual = io::rename_exclusive(from, to);
    if (actual) {
        return actual;
    }
    try {
        std::ofstream{to / bundle::image_name, std::ios::binary}.put('x');
    } catch (...) {
        return std::make_error_code(std::errc::io_error);
    }
    return {};
}
std::error_code unreadable_after_commit(const std::filesystem::path& from,
                                        const std::filesystem::path& to) noexcept {
    const auto actual = io::rename_exclusive(from, to);
    if (actual) {
        return actual;
    }
    try {
        std::filesystem::remove(to / bundle::record_name);
    } catch (...) {
        return std::make_error_code(std::errc::io_error);
    }
    return std::make_error_code(std::errc::io_error);
}
std::error_code occupied_after_gate(const std::filesystem::path& /*from*/,
                                    const std::filesystem::path& to) noexcept {
    try {
        std::filesystem::create_directory(to);
        std::ofstream{to / bundle::record_name} << "another run";
    } catch (...) {
        return std::make_error_code(std::errc::io_error);
    }
    return std::make_error_code(std::errc::io_error);
}
unsigned closed_streams = 0;
int close_stream(std::FILE* file) {
    ++closed_streams;
    // Consumes the owned stream even when the injected close operation reports failure.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return std::fclose(file);
}
int fail_close(std::FILE* file) {
    static_cast<void>(close_stream(file));
    return EOF;
}
int fail_flush(std::FILE* /*file*/) {
    return EOF;
}
std::size_t fail_write(const void* /*data*/, std::size_t /*size*/, std::size_t /*count*/,
                       std::FILE* /*file*/) {
    return 0;
}
struct ManifestWriter {
    std::string content;
    io::BundleStream stream;
    bool remove_record = false;
    bool extra_file = false;
};
core::Result<void> write_manifest(void* const state, const io::BundleSlot& slot) {
    auto const& writer = *static_cast<ManifestWriter*>(state);
    const auto written = io::write_bytes(slot, writer.content, {}, writer.stream);
    if (!written) {
        return written;
    }
    if (writer.remove_record) {
        std::filesystem::remove(slot.path);
    }
    if (writer.extra_file) {
        std::ofstream{slot.path.parent_path() / "foreign.txt"} << "foreign";
    }
    return {};
}
core::Result<std::string> publish(const std::filesystem::path& output, WrittenBundle& written,
                                  io::PublishRename commit) {
    const std::array files{
        io::BundleFile{
            .relative = bundle::image_name,
            .state = &written.image,
            .write = write_text,
        },
        io::BundleFile{
            .relative = bundle::record_name,
            .state = &written.record,
            .write = write_text,
        },
    };
    return io::publish_bundle(utf8_spelling(output), files, {}, commit,
                              host::bundle_validation(written.expected));
}
} // namespace
TEST_CASE("A completed bundle rename is reconciled after a lost reply", "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-reconcile"};
    auto written = written_bundle();
    const auto output = temporary.path / "result";
    const auto result = publish(output, written, lost_reply);
    REQUIRE(result);
    CHECK(host::verify_bundle(utf8_spelling(output), {}));
    CHECK(!std::filesystem::exists(temporary.path / "result.staging-0"));
}
TEST_CASE("An unresolved bundle commit preserves staging through destruction",
          "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-unknown"};
    auto written = written_bundle();
    const auto result = publish(temporary.path / "result", written, ambiguous_refusal);
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::publication_unknown);
    CHECK(result.error().publication == core::Publication::unknown);
    CHECK(host::verify_bundle(utf8_spelling(temporary.path / "result.staging-0"), {}));
}
TEST_CASE("Postcommit integrity failure retains completed publication", "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-integrity"};
    auto written = written_bundle();
    const auto output = temporary.path / "result";
    const auto result = publish(output, written, corrupt_after_commit);
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::output_verify);
    CHECK(result.error().publication == core::Publication::completed);
    CHECK(std::filesystem::is_directory(output));
    CHECK(std::filesystem::exists(output / bundle::record_name));
}
TEST_CASE("Staged bundle disagreement prevents any commit", "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-stage"};
    auto written = written_bundle();
    written.image = "not a PNG";
    const auto result = publish(temporary.path / "result", written, io::rename_exclusive);
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::output_verify);
    CHECK(result.error().publication == core::Publication::not_published);
    CHECK(empty_directory(temporary.path));
}
TEST_CASE("Owned publication with a lost reply and unreadable record stays completed",
          "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-unreadable"};
    auto written = written_bundle();
    const auto output = temporary.path / "result";
    const auto result = publish(output, written, unreadable_after_commit);
    REQUIRE(!result);
    CHECK(result.error().publication == core::Publication::completed);
    CHECK(result.error().code == core::ErrorCode::output_verify);
    CHECK(std::filesystem::exists(output / bundle::image_name));
}
TEST_CASE("Another run at the destination is preserved during reconciliation",
          "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-occupied"};
    auto written = written_bundle();
    const auto output = temporary.path / "result";
    const auto result = publish(output, written, occupied_after_gate);
    REQUIRE(!result);
    CHECK(result.error().publication == core::Publication::unknown);
    CHECK(file_contents(output / bundle::record_name) == "another run");
    CHECK(host::verify_bundle(utf8_spelling(temporary.path / "result.staging-0"), {}));
}
TEST_CASE("Manifest writer completion and staged reread failures prevent publication",
          "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-writer"};
    auto written = written_bundle();
    ManifestWriter manifest{.content = written.record, .stream = {}};
    closed_streams = 0;
    SECTION("write fails") {
        manifest.stream.write = fail_write;
    }
    SECTION("flush fails but close still runs once") {
        manifest.stream.flush = fail_flush;
        manifest.stream.close = close_stream;
    }
    SECTION("close failure is not retried") {
        manifest.stream.close = fail_close;
    }
    SECTION("record cannot be reread") {
        manifest.remove_record = true;
    }
    SECTION("staged inventory has an extra file") {
        manifest.extra_file = true;
    }
    const std::array files{
        io::BundleFile{
            .relative = bundle::image_name,
            .state = &written.image,
            .write = write_text,
        },
        io::BundleFile{
            .relative = bundle::record_name,
            .state = &manifest,
            .write = write_manifest,
        },
    };
    const auto output = temporary.path / "result";
    const auto result = io::publish_bundle(utf8_spelling(output), files, {}, io::rename_exclusive,
                                           host::bundle_validation(written.expected));
    REQUIRE(!result);
    CHECK(!std::filesystem::exists(output));
    if (manifest.stream.close == close_stream || manifest.stream.close == fail_close) {
        CHECK(closed_streams == 1);
    }
    if (manifest.extra_file) {
        CHECK(result.error().publication == core::Publication::unknown);
        CHECK(std::filesystem::exists(temporary.path / "result.staging-0" / "foreign.txt"));
    } else {
        CHECK(empty_directory(temporary.path));
    }
}
TEST_CASE("Publication refuses missing writers and oversized tables before staging", "[bundle]") {
    const TemporaryDirectory temporary{"docenhance-bundle-table"};
    const auto output = temporary.path / "result";
    const auto before = std::distance(std::filesystem::directory_iterator{temporary.path},
                                      std::filesystem::directory_iterator{});
    const io::BundleFile missing_writer{
        .relative = "image.png",
        .state = nullptr,
        .write = nullptr,
    };
    const std::array missing{missing_writer};
    REQUIRE(io::publish_bundle(utf8_spelling(output), missing).error().code ==
            core::ErrorCode::argument);
    std::string content = "unused";
    const std::vector<io::BundleFile> oversized(
        io::bundle_max_entries + 1,
        {.relative = "image.png", .state = &content, .write = write_text});
    REQUIRE(io::publish_bundle(utf8_spelling(output), oversized).error().code ==
            core::ErrorCode::argument);
    REQUIRE(std::distance(std::filesystem::directory_iterator{temporary.path},
                          std::filesystem::directory_iterator{}) == before);
}
TEST_CASE("Staging bounds nested owned entries and cleans every acquired directory", "[bundle]") {
    const TemporaryDirectory temporary{"docenhance-bundle-depth"};
    std::string relative;
    for (std::size_t i = 0; i < io::bundle_max_entries - 1; ++i) {
        relative += "d/";
    }
    relative += 'i';
    std::string content = "never written";
    const io::BundleFile file{.relative = relative, .state = &content, .write = write_text};
    const io::BundleFile extra{.relative = "extra", .state = &content, .write = write_text};
    const std::array files{file, extra};
    const auto refused = io::publish_bundle(utf8_spelling(temporary.path / "result"), files);
    REQUIRE(!refused);
    REQUIRE(refused.error().code == core::ErrorCode::resource);
    REQUIRE(refused.error().publication == core::Publication::not_published);
    REQUIRE(std::filesystem::directory_iterator{temporary.path} ==
            std::filesystem::directory_iterator{});
}
TEST_CASE("Native absolute publication names cannot escape staging", "[bundle]") {
    const TemporaryDirectory temporary{"docenhance-bundle-component"};
    const auto escaped = temporary.path / "escape";
    const auto relative = utf8_spelling(escaped);
    std::string content = "never written";
    const io::BundleFile file{.relative = relative, .state = &content, .write = write_text};
    const std::array files{file};
    const auto refused = io::publish_bundle(utf8_spelling(temporary.path / "result"), files);
    REQUIRE(!refused);
    REQUIRE(refused.error().publication == core::Publication::not_published);
    REQUIRE(!std::filesystem::exists(escaped));
    REQUIRE(std::filesystem::directory_iterator{temporary.path} ==
            std::filesystem::directory_iterator{});
}
TEST_CASE("Staging refuses nonportable artifact paths and leaves no owned effects", "[bundle]") {
    const TemporaryDirectory temporary{"docenhance-artifact-path"};
    std::string content = "never written";
    for (const auto& path : {
             std::string(129, 'a'),
             std::string{"a\0b", 3},
             std::string{"\xff"},
             std::string{"a:b"},
             std::string{"a\\b"},
             std::string{"a//b"},
         }) {
        const io::BundleFile file{
            .relative = path,
            .state = &content,
            .write = write_text,
        };
        const std::array files{file};
        const auto refused = io::publish_bundle(utf8_spelling(temporary.path / "result"), files);
        REQUIRE(!refused);
        CHECK(refused.error().code == core::ErrorCode::argument);
        CHECK(refused.error().publication == core::Publication::not_published);
        CHECK(std::filesystem::directory_iterator{temporary.path} ==
              std::filesystem::directory_iterator{});
    }
}
} // namespace docenhance::tests
