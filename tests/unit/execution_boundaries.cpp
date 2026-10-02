// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "cancellation_probe.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/verify.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/bundle.hpp"
#include "entry_identity.hpp"
#include "png_context.hpp"
#include "png_reader.hpp"
#include "png_rows.hpp"
#include "processor.hpp"
#include "publication.hpp"
#include "temporary_directory.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <system_error>

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
    unsigned calls = 0;
};
core::Result<void> throw_validation(void* const state, const std::string& /*directory*/,
                                    const core::Cancellation& /*cancellation*/) {
    auto& injection = *static_cast<ValidationInjection*>(state);
    if (++injection.calls == 1) {
        if (injection.preparing) {
            throw std::bad_alloc{};
        }
        return {};
    }
    throw std::runtime_error("Cannot inspect publication");
}
std::error_code ambiguous(const std::filesystem::path& /*from*/,
                          const std::filesystem::path& /*to*/) noexcept {
    return std::make_error_code(std::errc::io_error);
}
core::Result<void> write_text(void* /*state*/, const io::BundleSlot& slot) {
    return io::write_bytes(slot, "owned bytes");
}
std::stop_source& execution_stop() {
    static std::stop_source source;
    return source;
}
unsigned closes = 0;
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
    written_bytes += written;
    static_cast<void>(execution_stop().request_stop());
    return written;
}
std::size_t short_write_and_stop(const void* /*data*/, std::size_t /*size*/, std::size_t /*count*/,
                                 std::FILE* /*file*/) {
    static_cast<void>(execution_stop().request_stop());
    return 0;
}
int failed_flush(std::FILE* /*file*/) {
    return EOF;
}
struct Writer {
    core::Cancellation cancellation;
    io::BundleStream operations;
    std::optional<core::ErrorCode> error;
};
core::Result<void> write_controlled(void* const state, const io::BundleSlot& slot) {
    auto& writer = *static_cast<Writer*>(state);
    const std::string content(std::size_t{128} * 1024, 'a');
    auto result = io::write_bytes(slot, content, writer.cancellation, writer.operations);
    if (!result) {
        writer.error = result.error().code;
    }
    return result;
}
bool failed_read_and_stop(void* const state, std::span<std::uint8_t> /*bytes*/) noexcept {
    static_cast<void>(static_cast<std::stop_source*>(state)->request_stop());
    return false;
}
unsigned writer_calls = 0;
core::Result<void> stop_between_writers(void* /*state*/, const io::BundleSlot& slot) {
    ++writer_calls;
    auto result = io::write_bytes(slot, "owned");
    static_cast<void>(execution_stop().request_stop());
    return result;
}
} // namespace
TEST_CASE(
    "Verification admission rejects invalid requests before stop and avoids cancelled ports") {
    RejectingProcessor processor;
    CountingVerifier verifier;
    execution_stop() = std::stop_source{};
    static_cast<void>(execution_stop().request_stop());
    const core::Cancellation control{execution_stop().get_token()};
    contract::Invocation invocation;
    invocation.command = contract::Command::verify;
    REQUIRE(app::dispatch(invocation, processor, verifier, control).exit_code() ==
            core::ExitCode::invocation);
    invocation.subject = "absent";
    const auto outcome = app::dispatch(invocation, processor, verifier, control);
    REQUIRE(outcome.exit_code() == core::ExitCode::cancelled);
    REQUIRE(std::get<app::Failure>(outcome.payload).error.publication ==
            core::Publication::not_started);
    REQUIRE(verifier.calls == 0);
}
TEST_CASE("Publication exception containment respects the irreversible boundary", "[publication]") {
    for (const bool preparing : {true, false}) {
        for (const auto commit : {io::rename_exclusive, ambiguous}) {
            const TemporaryDirectory temporary{"docenhance-publication-exception"};
            const auto target = temporary.path / "result";
            const io::BundleFile file{
                .relative = "result.png",
                .state = nullptr,
                .write = write_text,
            };
            const std::array files{file};
            ValidationInjection injection{.preparing = preparing};
            const io::BundleValidation validation{
                .state = &injection,
                .validate = throw_validation,
            };
            const auto result =
                io::publish_bundle(utf8_spelling(target), files, {}, commit, validation);
            REQUIRE(!result);
            if (preparing) {
                CHECK(result.error().code == core::ErrorCode::resource);
                CHECK(result.error().publication == core::Publication::not_published);
                CHECK(empty_directory(temporary.path));
            } else if (commit == io::rename_exclusive) {
                CHECK(result.error().code == core::ErrorCode::output_verify);
                CHECK(result.error().publication == core::Publication::completed);
                CHECK(std::filesystem::exists(target / "result.png"));
            } else {
                CHECK(result.error().code == core::ErrorCode::publication_unknown);
                CHECK(result.error().publication == core::Publication::unknown);
                CHECK(std::filesystem::exists(temporary.path / "result.staging-0" / "result.png"));
            }
        }
    }
}
TEST_CASE("Manifest cancellation closes once and cannot erase delayed stream failures") {
    for (const unsigned failure : {0U, 1U, 2U, 3U}) {
        const TemporaryDirectory temporary{"docenhance-manifest-stop"};
        execution_stop() = std::stop_source{};
        closes = 0;
        written_bytes = 0;
        Writer writer{
            .cancellation = core::Cancellation{execution_stop().get_token()},
            .operations =
                {
                    .write = failure == 1 ? short_write_and_stop : stop_after_write,
                    .flush = failure == 2 ? failed_flush : std::fflush,
                    .close = failure == 3 ? failed_close : counted_close,
                },
            .error = std::nullopt,
        };
        const io::BundleFile file{
            .relative = "run.json",
            .state = &writer,
            .write = write_controlled,
        };
        const std::array files{file};
        const auto result = io::publish_bundle(utf8_spelling(temporary.path / "result"), files,
                                               writer.cancellation);
        REQUIRE(!result);
        CHECK(result.error().code ==
              (failure == 0 ? core::ErrorCode::cancelled : core::ErrorCode::output));
        CHECK(result.error().publication == core::Publication::not_published);
        CHECK(writer.error == result.error().code);
        CHECK(written_bytes == (failure == 1 ? 0 : std::size_t{64} * 1024));
        CHECK(closes == 1);
        CHECK(empty_directory(temporary.path));
    }
}
TEST_CASE("Cancellation between bundle writers prevents later callbacks and cleans ownership") {
    const TemporaryDirectory temporary{"docenhance-writer-stop"};
    execution_stop() = std::stop_source{};
    writer_calls = 0;
    const std::array files{
        io::BundleFile{.relative = "result.png", .state = nullptr, .write = stop_between_writers},
        io::BundleFile{.relative = "run.json", .state = nullptr, .write = stop_between_writers},
    };
    const auto result = io::publish_bundle(utf8_spelling(temporary.path / "result"), files,
                                           core::Cancellation{execution_stop().get_token()});
    REQUIRE(!result);
    CHECK(result.error().code == core::ErrorCode::cancelled);
    CHECK(result.error().publication == core::Publication::not_published);
    CHECK(writer_calls == 1);
    CHECK(empty_directory(temporary.path));
}
TEST_CASE("A failed native PNG read outranks a concurrently requested stop") {
    std::stop_source source;
    core::Budget budget{std::size_t{1} * 1024 * 1024};
    {
        io::PngContext context{budget, false, core::Cancellation{source.get_token()}};
        io::PngInput input{.state = &source, .read = failed_read_and_stop, .remaining = 8};
        const auto result = io::decode_png(context, input, {});
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::input);
        CHECK(source.stop_requested());
    }
    REQUIRE(budget.used() == 0);
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
    const auto path = temporary.path / "result.png";
    const auto parent = io::EntryLease::directory(temporary.path);
    io::EntryLease created;
    const io::BundleSlot slot{.path = path, .created = &created, .parent = parent.identity()};
    REQUIRE(io::encode_png(slot, image.view().as_const(), budget));
    const auto held = budget.used();
    bool complete = false;
    for (std::size_t after = 0; after < 256; ++after) {
        const CheckpointStop stop{core::Checkpoint::verification, after};
        const auto verified =
            io::verify_png_image(path, image.view().as_const(), budget, stop.cancellation());
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
