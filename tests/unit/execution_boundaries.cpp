// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "bundle_stage.hpp"
#include "cancellation_probe.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/codec/png_output.hpp"
#include "docenhance/color/profile.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/fs/publication.hpp"
#include "docenhance/fs/snapshot.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "entry_identity.hpp"
#include "file_contents.hpp"
#include "native_publication.hpp"
#include "processor.hpp"
#include "temporary_directory.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace docenhance::tests {
namespace {
class CountingVerifier final : public app::Verifier {
  public:
    unsigned calls = 0;
    core::Result<app::Verified> verify(const app::VerifyRequest& /*request*/,
                                       const core::Cancellation& /*cancellation*/) override {
        ++calls;
        return core::failure(core::ErrorCode::input, "Unexpected verification effect");
    }
};
struct ValidationInjection {
    bool preparing = false;
    bool enabled = true;
    unsigned calls = 0;
};
core::Result<void> throw_validation(void* const state, const std::string& /*directory*/,
                                    const core::Cancellation& /*cancellation*/) {
    auto& injection = *static_cast<ValidationInjection*>(state);
    ++injection.calls;
    if (!injection.enabled) {
        return {};
    }
    if (injection.calls == 1) {
        if (injection.preparing) {
            throw std::bad_alloc{};
        }
        return {};
    }
    throw std::runtime_error("Cannot inspect publication");
}
unsigned commits = 0;
std::error_code counted_commit(const std::filesystem::path& from,
                               const std::filesystem::path& to) noexcept {
    ++commits;
    return fs::rename_exclusive(from, to);
}
std::error_code ambiguous(const std::filesystem::path& /*from*/,
                          const std::filesystem::path& /*to*/) noexcept {
    ++commits;
    return std::make_error_code(std::errc::io_error);
}
std::stop_source& execution_stop() {
    static std::stop_source source;
    return source;
}
unsigned closes = 0;
unsigned flushes = 0;
unsigned writes = 0;
bool stop_writing = true;
std::size_t written_bytes = 0;
int counted_close(std::FILE* file) {
    ++closes;
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return std::fclose(file);
}
int failed_close(std::FILE* file) {
    static_cast<void>(counted_close(file));
    return EOF;
}
std::size_t stop_after_write(const void* const data, std::size_t size, std::size_t count,
                             std::FILE* file) {
    const auto written = std::fwrite(data, size, count, file);
    ++writes;
    written_bytes += written;
    if (stop_writing) {
        static_cast<void>(execution_stop().request_stop());
    }
    return written;
}
std::size_t short_write_and_stop(const void* /*data*/, std::size_t /*size*/, std::size_t /*count*/,
                                 std::FILE* /*file*/) {
    ++writes;
    static_cast<void>(execution_stop().request_stop());
    return 0;
}
int failed_flush(std::FILE* /*file*/) {
    ++flushes;
    return EOF;
}
int counted_flush(std::FILE* file) {
    ++flushes;
    return std::fflush(file);
}
core::Result<void> verify_png_snapshot(const std::string& path, codec::PngImage intended,
                                       core::Budget& budget,
                                       const core::Cancellation& cancellation) {
    auto snapshot =
        fs::read_source_snapshot(path, budget, cancellation, core::source_encoded_bytes_max);
    if (!snapshot) {
        return std::unexpected(std::move(snapshot.error()));
    }
    return codec::compare_png(snapshot->encoded.bytes(), intended, budget, cancellation);
}
struct BetweenFilesStop {
    static inline std::filesystem::path stage;
    static inline bool enabled = false, observed = false, exact = false, absent = false;
    static inline bool stopped = false, failed = false;
    static inline unsigned encodes = 0;
    static void reset(const std::filesystem::path& directory, bool stop) {
        stage = directory;
        enabled = stop;
        observed = exact = absent = stopped = failed = false;
        encodes = 0;
    }
    static bool observe(core::Checkpoint at) noexcept {
        if (at == core::Checkpoint::encode) {
            ++encodes;
        }
        if (!stopped && at == core::Checkpoint::staging) {
            try {
                if (std::filesystem::exists(stage / "result.png")) {
                    observed = true;
                    exact = file_contents(stage / "result.png") == "owned";
                    absent = !std::filesystem::exists(stage / "run.json");
                    stopped = enabled;
                }
            } catch (...) {
                failed = stopped = true;
            }
        }
        return stopped;
    }
};
void check_publication_success(const fs::PublishResult<std::string>& result,
                               const std::filesystem::path& target,
                               const TemporaryDirectory& temporary, std::string_view content) {
    REQUIRE(result);
    CHECK(file_contents(target / "result.png") == content);
    CHECK(!std::filesystem::exists(temporary.path / "result.staging-0"));
}
void check_precommit_failure(const fs::PublishFailure& failure,
                             const TemporaryDirectory& temporary) {
    CHECK(failure.error.code == core::ErrorCode::resource);
    CHECK(failure.publication == core::Publication::not_published);
    CHECK(empty_directory(temporary.path));
}
void check_postcommit_failure(const fs::PublishFailure& failure,
                              const std::filesystem::path& target, std::string_view content) {
    CHECK(failure.error.code == core::ErrorCode::output_verify);
    CHECK(failure.publication == core::Publication::completed);
    CHECK(file_contents(target / "result.png") == content);
}
void check_ambiguous_publication(const fs::PublishFailure& failure,
                                 const std::filesystem::path& target,
                                 const TemporaryDirectory& temporary, std::string_view content) {
    CHECK(failure.error.code == core::ErrorCode::publication_unknown);
    CHECK(failure.publication == core::Publication::unknown);
    CHECK(!std::filesystem::exists(target));
    CHECK(file_contents(temporary.path / "result.staging-0" / "result.png") == content);
}
void publication_exception_case(bool enabled, bool preparing, fs::PublishRename commit) {
    const TemporaryDirectory temporary{"docenhance-publication-exception"};
    const auto target = temporary.path / "result";
    constexpr std::string_view content = "owned bytes";
    const std::array files{
        fs::BundleFile{
            .relative = "result.png",
            .bytes = std::as_bytes(std::span{content}),
        },
    };
    ValidationInjection injection{.preparing = preparing, .enabled = enabled};
    const fs::BundleValidation validation{
        .state = &injection,
        .validate = throw_validation,
    };
    commits = 0;
    const auto result = fs::publish_bundle(utf8_spelling(target), files, {}, commit, validation);
    const bool refused_before_commit = enabled && preparing;
    CHECK(commits == (refused_before_commit ? 0U : 1U));
    CHECK(injection.calls == (refused_before_commit || commit == ambiguous ? 1U : 2U));
    if (!enabled && commit == counted_commit) {
        check_publication_success(result, target, temporary, content);
        return;
    }
    REQUIRE(!result);
    if (refused_before_commit) {
        check_precommit_failure(result.error(), temporary);
    } else if (commit == counted_commit) {
        check_postcommit_failure(result.error(), target, content);
    } else {
        check_ambiguous_publication(result.error(), target, temporary, content);
    }
}
} // namespace
TEST_CASE(
    "Verification admission rejects invalid requests before stop and avoids cancelled ports") {
    RejectingProcessor processor;
    CountingVerifier verifier;
    execution_stop() = std::stop_source{};
    static_cast<void>(execution_stop().request_stop());
    const core::Cancellation control{execution_stop().get_token()};
    app::Invocation invocation;
    invocation.command = app::Command::verify;
    REQUIRE(app::dispatch(invocation, processor, verifier, control).exit_code() ==
            core::ExitCode::invocation);
    invocation.operands = {"absent"};
    const auto outcome = app::dispatch(invocation, processor, verifier, control);
    REQUIRE(outcome.exit_code() == core::ExitCode::cancelled);
    REQUIRE(std::get<app::Failure>(outcome.payload).publication == core::Publication::not_started);
    REQUIRE(verifier.calls == 0);
}
TEST_CASE("Publication exception containment respects the irreversible boundary", "[publication]") {
    for (const bool enabled : {false, true}) {
        for (const bool preparing : {true, false}) {
            for (const auto commit : {counted_commit, ambiguous}) {
                publication_exception_case(enabled, preparing, commit);
            }
        }
    }
}
TEST_CASE("Manifest cancellation closes once and cannot erase delayed stream failures") {
    for (const unsigned failure : {4U, 0U, 1U, 2U, 3U}) {
        const TemporaryDirectory temporary{"docenhance-manifest-stop"};
        const auto target = temporary.path / "result";
        fs::Stage stage;
        REQUIRE(fs::reserve_stage(stage, target, {}));
        const auto slot = fs::place(stage, "run.json");
        REQUIRE(slot);
        const std::string content(std::size_t{128} * 1024, 'a');
        execution_stop() = std::stop_source{};
        const core::Cancellation cancellation{execution_stop().get_token()};
        closes = flushes = writes = 0;
        written_bytes = 0;
        stop_writing = failure != 4;
        const fs::BundleStream operations{
            .write = failure == 1 ? short_write_and_stop : stop_after_write,
            .flush = failure == 2 ? failed_flush : counted_flush,
            .close = failure == 3 ? failed_close : counted_close,
        };
        auto result =
            fs::write_bytes(*slot, std::as_bytes(std::span{content}), cancellation, operations);
        CHECK(closes == 1);
        CHECK(flushes == 1);
        CHECK(writes == (failure == 4 ? 2U : 1U));
        CHECK(written_bytes == (failure == 1   ? 0
                                : failure == 4 ? std::size_t{128} * 1024
                                               : std::size_t{64} * 1024));
        if (failure == 4) {
            REQUIRE(result);
            CHECK(!execution_stop().stop_requested());
            CHECK(file_contents(slot->path) == content);
            CHECK(stage.owns_entries());
            CHECK(stage.cleanup());
        } else {
            REQUIRE(!result);
            CHECK(execution_stop().stop_requested());
            const auto code = result.error().code;
            CHECK(code == (failure == 0 ? core::ErrorCode::cancelled : core::ErrorCode::output));
            const auto abandoned = fs::abandon(stage, std::move(result.error()));
            CHECK(abandoned.error.code == code);
            CHECK(abandoned.publication == core::Publication::not_published);
        }
        CHECK(!stage.owned);
        CHECK(!std::filesystem::exists(target));
        CHECK(empty_directory(temporary.path));
    }
}
TEST_CASE("Cancellation between bundle files prevents later writes and cleans ownership") {
    for (const bool stopped : {false, true}) {
        const TemporaryDirectory temporary{"docenhance-writer-stop"};
        const auto target = temporary.path / "result";
        BetweenFilesStop::reset(temporary.path / "result.staging-0", stopped);
        constexpr std::string_view content = "owned";
        const auto bytes = std::as_bytes(std::span{content});
        const std::array files{
            fs::BundleFile{.relative = "result.png", .bytes = bytes},
            fs::BundleFile{.relative = "run.json", .bytes = bytes},
        };
        commits = 0;
        const auto result =
            fs::publish_bundle(utf8_spelling(target), files,
                               core::Cancellation{{}, &BetweenFilesStop::observe}, counted_commit);
        CHECK(BetweenFilesStop::observed);
        CHECK(BetweenFilesStop::exact);
        CHECK(BetweenFilesStop::absent);
        CHECK(!BetweenFilesStop::failed);
        CHECK(BetweenFilesStop::stopped == stopped);
        CHECK(BetweenFilesStop::encodes == (stopped ? 2U : 4U));
        CHECK(commits == (stopped ? 0U : 1U));
        if (stopped) {
            REQUIRE(!result);
            CHECK(result.error().error.code == core::ErrorCode::cancelled);
            CHECK(result.error().publication == core::Publication::not_published);
            CHECK(empty_directory(temporary.path));
        } else {
            REQUIRE(result);
            CHECK(file_contents(target / "result.png") == content);
            CHECK(file_contents(target / "run.json") == content);
            CHECK(!std::filesystem::exists(temporary.path / "result.staging-0"));
        }
    }
}
TEST_CASE("Every wide PNG reread checkpoint cancels with full resource refunds") {
    const TemporaryDirectory temporary{"docenhance-verification-stop"};
    core::Budget budget{std::size_t{8} * 1024 * 1024};
    auto image = image::Plane<std::uint8_t>::allocate(budget, 200000, 1).value();
    std::uint32_t value = 17;
    const auto samples = image.view().row(0);
    for (auto& sample : samples) {
        value = (value * 1664525U) + 1013904223U;
        sample = static_cast<std::uint8_t>(value >> 24U);
    }
    const auto canonical = color::make_output_profile(budget, true).value();
    auto profile = image::Plane<std::uint8_t>::allocate(
                       budget, static_cast<std::uint32_t>(canonical.size()), 1)
                       .value();
    std::ranges::transform(canonical.bytes(), profile.view().row(0).begin(),
                           [](std::byte byte) { return std::to_integer<std::uint8_t>(byte); });
    const codec::PngImage intended{
        .shape = {.width = 200000, .height = 1, .depth = image::PngDepth()},
        .pixels = image.view().as_const(),
        .profile = std::as_bytes(profile.view().row(0)),
        .resolution = std::nullopt,
    };
    const auto encoded = codec::encode_png(intended, budget);
    REQUIRE(encoded);
    const auto path = temporary.path / "result.png";
    const auto parent = fs::EntryLease::directory(temporary.path);
    fs::EntryLease created;
    const fs::BundleSlot slot{.path = path, .created = &created, .parent = parent.identity()};
    REQUIRE(fs::write_bytes(slot, encoded->bytes()));
    const auto identity = fs::identify(encoded->bytes());
    const auto stored = fs::identify_slot(slot, core::source_encoded_bytes_max);
    REQUIRE(identity);
    REQUIRE(stored);
    CHECK(*stored == *identity);
    const auto held = budget.used();
    bool complete = false;
    for (std::size_t after = 0; after < 256; ++after) {
        const CheckpointStop stop{core::Checkpoint::verification, after};
        const auto verified =
            verify_png_snapshot(utf8_spelling(path), intended, budget, stop.cancellation());
        if (CheckpointStop::stopped()) {
            REQUIRE(!verified);
            CHECK(verified.error().code == core::ErrorCode::cancelled);
        } else {
            REQUIRE(verified);
            CHECK(after > 64);
            complete = true;
            break;
        }
        REQUIRE(budget.used() == held);
    }
    REQUIRE(complete);
    REQUIRE(budget.used() == held);
}
} // namespace docenhance::tests
