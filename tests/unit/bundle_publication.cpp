// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "bundle_fixture.hpp"
#include "bundle_stage.hpp"
#include "bundle_verify.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/artifact_limits.hpp"
#include "docenhance/fs/publication.hpp"
#include "docenhance/record/record.hpp"
#include "file_access.hpp"
#include "file_contents.hpp"
#include "native_publication.hpp"
#include "temporary_directory.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
namespace docenhance::tests {
namespace {
using bundle_fixture::bytes;
using bundle_fixture::written_bundle;
using bundle_fixture::WrittenBundle;
std::error_code lost_reply(const std::filesystem::path& from,
                           const std::filesystem::path& to) noexcept {
    const auto actual = fs::rename_exclusive(from, to);
    return actual ? actual : std::make_error_code(std::errc::io_error);
}
std::error_code ambiguous_refusal(const std::filesystem::path& /*unused*/,
                                  const std::filesystem::path& /*unused*/) noexcept {
    return std::make_error_code(std::errc::io_error);
}
std::error_code corrupt_after_commit(const std::filesystem::path& from,
                                     const std::filesystem::path& to) noexcept {
    const auto actual = fs::rename_exclusive(from, to);
    if (actual) {
        return actual;
    }
    try {
        std::ofstream{to / record::image_name, std::ios::binary}.put('x');
    } catch (...) {
        return std::make_error_code(std::errc::io_error);
    }
    return {};
}
std::error_code unreadable_after_commit(const std::filesystem::path& from,
                                        const std::filesystem::path& to) noexcept {
    const auto actual = fs::rename_exclusive(from, to);
    if (actual) {
        return actual;
    }
    try {
        std::filesystem::remove(to / record::record_name);
    } catch (...) {
        return std::make_error_code(std::errc::io_error);
    }
    return std::make_error_code(std::errc::io_error);
}
std::error_code occupied_after_gate(const std::filesystem::path& /*from*/,
                                    const std::filesystem::path& to) noexcept {
    try {
        std::filesystem::create_directory(to);
        std::ofstream{to / record::record_name} << "another run";
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
struct RereadFailure {
    explicit RereadFailure(host::ExpectedBundle& owner) : expected(owner) {}
    std::reference_wrapper<host::ExpectedBundle> expected;
    bool remove_record = false;
    bool extra_file = false;
};
core::Result<void> fail_staged_reread(void* const state, const std::string& directory,
                                      const core::Cancellation& cancellation) {
    const auto& failure = *static_cast<RereadFailure*>(state);
    const auto root = fs::utf8_path(directory);
    if (failure.remove_record) {
        std::filesystem::remove(root / record::record_name);
    }
    if (failure.extra_file) {
        std::ofstream{root / "foreign.txt"} << "foreign";
    }
    const auto validation = host::bundle_validation(failure.expected.get());
    return validation.validate(validation.state, directory, cancellation);
}
fs::PublishResult<std::string> publish(const std::filesystem::path& output, WrittenBundle& written,
                                       fs::PublishRename commit) {
    const std::array files{
        fs::BundleFile{
            .relative = record::image_name,
            .bytes = bytes(written.image),
        },
        fs::BundleFile{
            .relative = record::record_name,
            .bytes = bytes(written.record),
        },
    };
    return fs::publish_bundle(utf8_spelling(output), files, {}, commit,
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
    CHECK(result.error().error.code == core::ErrorCode::publication_unknown);
    CHECK(result.error().publication == core::Publication::unknown);
    CHECK(host::verify_bundle(utf8_spelling(temporary.path / "result.staging-0"), {}));
}
TEST_CASE("Postcommit integrity failure retains completed publication", "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-integrity"};
    auto written = written_bundle();
    const auto output = temporary.path / "result";
    const auto result = publish(output, written, corrupt_after_commit);
    REQUIRE(!result);
    CHECK(result.error().error.code == core::ErrorCode::output_verify);
    CHECK(result.error().publication == core::Publication::completed);
    CHECK(std::filesystem::is_directory(output));
    CHECK(std::filesystem::exists(output / record::record_name));
}
TEST_CASE("Staged bundle disagreement prevents any commit", "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-stage"};
    auto written = written_bundle();
    written.image = "not a PNG";
    const auto result = publish(temporary.path / "result", written, fs::rename_exclusive);
    REQUIRE(!result);
    CHECK(result.error().error.code == core::ErrorCode::output_verify);
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
    CHECK(result.error().error.code == core::ErrorCode::output_verify);
    CHECK(std::filesystem::exists(output / record::image_name));
}
TEST_CASE("Another run at the destination is preserved during reconciliation",
          "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-occupied"};
    auto written = written_bundle();
    const auto output = temporary.path / "result";
    const auto result = publish(output, written, occupied_after_gate);
    REQUIRE(!result);
    CHECK(result.error().publication == core::Publication::unknown);
    CHECK(file_contents(output / record::record_name) == "another run");
    CHECK(host::verify_bundle(utf8_spelling(temporary.path / "result.staging-0"), {}));
}
TEST_CASE("Byte writer completion errors close once and prevent publication",
          "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-writer"};
    const auto written = written_bundle();
    fs::BundleStream stream{.close = close_stream};
    closed_streams = 0;
    SECTION("write fails") {
        stream.write = fail_write;
    }
    SECTION("flush fails but close still runs once") {
        stream.flush = fail_flush;
    }
    SECTION("close failure is not retried") {
        stream.close = fail_close;
    }
    fs::Stage stage;
    REQUIRE(fs::reserve_stage(stage, temporary.path / "result", {}));
    const auto slot = fs::place(stage, record::record_name);
    REQUIRE(slot);
    auto completed = fs::write_bytes(*slot, bytes(written.record), {}, stream);
    REQUIRE(!completed);
    CHECK(completed.error().code == core::ErrorCode::output);
    CHECK(closed_streams == 1);
    const auto refused = fs::abandon(stage, std::move(completed.error()));
    CHECK(refused.publication == core::Publication::not_published);
    CHECK(empty_directory(temporary.path));
}
TEST_CASE("Staged record reread and extra inventory failures prevent publication",
          "[bundle][publication]") {
    const TemporaryDirectory temporary{"docenhance-bundle-reread"};
    auto written = written_bundle();
    RereadFailure failure{written.expected};
    SECTION("record cannot be reread") {
        failure.remove_record = true;
    }
    SECTION("staged inventory has an extra file") {
        failure.extra_file = true;
    }
    const std::array files{
        fs::BundleFile{.relative = record::image_name, .bytes = bytes(written.image)},
        fs::BundleFile{.relative = record::record_name, .bytes = bytes(written.record)},
    };
    const auto output = temporary.path / "result";
    const auto result = fs::publish_bundle(utf8_spelling(output), files, {}, fs::rename_exclusive,
                                           {.state = &failure, .validate = fail_staged_reread});
    REQUIRE(!result);
    CHECK(!std::filesystem::exists(output));
    if (failure.extra_file) {
        CHECK(result.error().publication == core::Publication::unknown);
        CHECK(std::filesystem::exists(temporary.path / "result.staging-0" / "foreign.txt"));
    } else {
        CHECK(result.error().publication == core::Publication::not_published);
        CHECK(empty_directory(temporary.path));
    }
}
TEST_CASE("Publication refuses empty tables missing commit and oversized tables before staging",
          "[bundle]") {
    const TemporaryDirectory temporary{"docenhance-bundle-table"};
    const auto output = utf8_spelling(temporary.path / "result");
    const std::string content = "unused";
    const std::array files{fs::BundleFile{.relative = "image.png", .bytes = bytes(content)}};
    REQUIRE(fs::publish_bundle(output, std::span<const fs::BundleFile>{}).error().error.code ==
            core::ErrorCode::argument);
    REQUIRE(fs::publish_bundle(output, files, {}, nullptr).error().error.code ==
            core::ErrorCode::argument);
    const std::vector<fs::BundleFile> oversized(fs::bundle_max_entries + 1,
                                                {.relative = "image.png", .bytes = bytes(content)});
    REQUIRE(fs::publish_bundle(output, oversized).error().error.code == core::ErrorCode::argument);
    CHECK(empty_directory(temporary.path));
}
TEST_CASE("Staging bounds nested owned entries and cleans every acquired directory", "[bundle]") {
    const TemporaryDirectory temporary{"docenhance-bundle-depth"};
    std::string relative;
    for (std::size_t i = 0; i < fs::bundle_max_entries - 1; ++i) {
        relative += "d/";
    }
    relative += 'i';
    const std::string content = "never written";
    const fs::BundleFile file{.relative = relative, .bytes = bytes(content)};
    const fs::BundleFile extra{.relative = "extra", .bytes = bytes(content)};
    const std::array files{file, extra};
    const auto refused = fs::publish_bundle(utf8_spelling(temporary.path / "result"), files);
    REQUIRE(!refused);
    REQUIRE(refused.error().error.code == core::ErrorCode::resource);
    REQUIRE(refused.error().publication == core::Publication::not_published);
    REQUIRE(std::filesystem::directory_iterator{temporary.path} ==
            std::filesystem::directory_iterator{});
}
TEST_CASE("Native absolute publication names cannot escape staging", "[bundle]") {
    const TemporaryDirectory temporary{"docenhance-bundle-component"};
    const auto escaped = temporary.path / "escape";
    const auto relative = utf8_spelling(escaped);
    const std::string content = "never written";
    const fs::BundleFile file{.relative = relative, .bytes = bytes(content)};
    const std::array files{file};
    const auto refused = fs::publish_bundle(utf8_spelling(temporary.path / "result"), files);
    REQUIRE(!refused);
    REQUIRE(refused.error().publication == core::Publication::not_published);
    REQUIRE(!std::filesystem::exists(escaped));
    REQUIRE(std::filesystem::directory_iterator{temporary.path} ==
            std::filesystem::directory_iterator{});
}
TEST_CASE("Staging refuses nonportable artifact paths and leaves no owned effects", "[bundle]") {
    const TemporaryDirectory temporary{"docenhance-artifact-path"};
    const std::string content = "never written";
    for (const auto& path : {
             std::string(129, 'a'),
             std::string{"a\0b", 3},
             std::string{"\xff"},
             std::string{"a:b"},
             std::string{"a\\b"},
             std::string{"a//b"},
         }) {
        const fs::BundleFile file{
            .relative = path,
            .bytes = bytes(content),
        };
        const std::array files{file};
        const auto refused = fs::publish_bundle(utf8_spelling(temporary.path / "result"), files);
        REQUIRE(!refused);
        CHECK(refused.error().error.code == core::ErrorCode::argument);
        CHECK(refused.error().publication == core::Publication::not_published);
        CHECK(std::filesystem::directory_iterator{temporary.path} ==
              std::filesystem::directory_iterator{});
    }
}
} // namespace docenhance::tests
