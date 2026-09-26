// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "cancellation_probe.hpp"
#include "docenhance/app/dispatch.hpp"
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/host/processor.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "png_fixture.hpp"
#include "publication.hpp"
#include "temporary_directory.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <stop_token>
#include <system_error>
#include <vector>

namespace docenhance::tests {
namespace {
constexpr std::size_t limit = std::size_t{4} * 1024 * 1024;
std::vector<std::uint8_t> png_bytes(bool interlaced) {
    return make_gray_png({
        .width = 8,
        .height = 8,
        .depth = 8,
        .interlaced = interlaced,
        .samples = std::vector<std::uint8_t>(64, 127),
    });
}
void write_file(const std::filesystem::path& file, std::span<const std::uint8_t> bytes) {
    std::ofstream stream{file, std::ios::binary};
    for (const auto byte : bytes) {
        stream.put(static_cast<char>(byte));
    }
    stream.close();
    REQUIRE(stream);
}
std::stop_source& commit_source() {
    static std::stop_source source;
    return source;
}
std::error_code cancel_after_gate(const std::filesystem::path& from,
                                  const std::filesystem::path& to) noexcept {
    static_cast<void>(commit_source().request_stop());
    return io::rename_exclusive(from, to);
}
std::error_code ambiguous_commit(const std::filesystem::path& from,
                                 const std::filesystem::path& to) noexcept {
    const auto actual = cancel_after_gate(from, to);
    return actual ? actual : std::make_error_code(std::errc::io_error);
}
std::error_code refused_commit(const std::filesystem::path& from,
                               const std::filesystem::path& to) noexcept {
    try {
        static_cast<void>(commit_source().request_stop());
        std::error_code error;
        std::filesystem::create_directory(to, error);
        return error ? error : io::rename_exclusive(from, to);
    } catch (...) {
        return std::make_error_code(std::errc::io_error);
    }
}
std::error_code unclean_commit(const std::filesystem::path& from,
                               const std::filesystem::path& /*to*/) noexcept {
    try {
        static_cast<void>(commit_source().request_stop());
        std::ofstream file{from / "foreign-entry"};
        file << "preserve this file";
        file.close();
        return file ? std::make_error_code(std::errc::file_exists)
                    : std::make_error_code(std::errc::io_error);
    } catch (...) {
        return std::make_error_code(std::errc::io_error);
    }
}
} // namespace
namespace {
void check_decode(const core::Result<image::Plane<std::uint8_t>>& result) {
    if (CheckpointStop::stopped()) {
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
    } else {
        REQUIRE(result);
    }
}
bool decode_attempt(std::span<const std::uint8_t> bytes, std::size_t after) {
    const CheckpointStop stop{core::Checkpoint::decode, after};
    core::Budget budget{limit};
    const io::PngLimits limits;
    {
        const auto result = io::decode_grayscale_png(bytes, budget, limits, stop.cancellation());
        check_decode(result);
    }
    REQUIRE(budget.used() == 0);
    return !CheckpointStop::stopped();
}
} // namespace
TEST_CASE("Every PNG decoding checkpoint cancels and refunds owned storage",
          "[cancellation][png]") {
    for (const bool interlaced : {false, true}) {
        const auto bytes = png_bytes(interlaced);
        bool completed = false;
        constexpr std::size_t attempts = 256;
        for (std::size_t after = 0; after < attempts; ++after) {
            if (decode_attempt(bytes, after)) {
                CHECK(after > 1);
                completed = true;
                break;
            }
        }
        REQUIRE(completed);
    }
}
TEST_CASE("Every PNG encoding checkpoint cleans unpublished output", "[cancellation][png]") {
    const TemporaryDirectory directory{"docenhance-stop"};
    core::Budget budget{limit};
    auto plane = image::Plane<std::uint8_t>::allocate(budget, 8, 8).value();
    std::ranges::fill(plane.view().storage(), UINT8_MAX);
    const auto held = budget.used();
    const auto output = utf8_spelling(directory.path / "result");
    bool completed = false;
    constexpr std::size_t attempts = 128;
    for (std::size_t after = 0; after < attempts; ++after) {
        const CheckpointStop stop{core::Checkpoint::encode, after};
        const auto result =
            io::publish_grayscale_png(output, plane.view().as_const(), budget, stop.cancellation());
        if (CheckpointStop::stopped()) {
            REQUIRE(!result);
            CHECK(result.error().code == core::ErrorCode::cancelled);
            CHECK(result.error().publication == core::Publication::not_published);
            CHECK(empty_directory(directory.path));
        } else {
            REQUIRE(result);
            completed = true;
        }
        CHECK(budget.used() == held);
        if (completed) {
            break;
        }
    }
    REQUIRE(completed);
}
TEST_CASE("Host cancellation preserves input and never publishes incomplete processing",
          "[cancellation]") {
    const TemporaryDirectory directory{"docenhance-stop"};
    const auto source = directory.path / "source.png";
    const auto bytes = png_bytes(true);
    write_file(source, bytes);
    const contract::Invocation request{
        .command = contract::Command::process,
        .subject = utf8_spelling(source),
        .output_directory = utf8_spelling(directory.path / "output"),
        .output_mode = "bw",
        .binarize = "sauvola",
    };
    host::Processor processor;
    for (const auto phase : {
             core::Checkpoint::admission,
             core::Checkpoint::decode,
             core::Checkpoint::allocation,
             core::Checkpoint::initialization,
             core::Checkpoint::processing,
             core::Checkpoint::staging,
             core::Checkpoint::encode,
             core::Checkpoint::commit,
         }) {
        const CheckpointStop stop{phase, 0};
        const auto result = app::dispatch(request, processor, stop.cancellation());
        REQUIRE(result.exit_code() == core::ExitCode::cancelled);
        const auto& error = std::get<app::Failure>(result.payload).error;
        const auto staged = phase == core::Checkpoint::encode || phase == core::Checkpoint::commit;
        CHECK(error.publication ==
              (staged ? core::Publication::not_published : core::Publication::not_started));
        CHECK(std::distance(std::filesystem::directory_iterator(directory.path),
                            std::filesystem::directory_iterator{}) == 1);
        std::ifstream original{source, std::ios::binary};
        for (const auto byte : bytes) {
            REQUIRE(original.get() == byte);
        }
    }
}
TEST_CASE("The final commit checkpoint is the cancellation cutoff", "[cancellation][io]") {
    const TemporaryDirectory directory{"docenhance-stop"};
    core::Budget budget{limit};
    auto plane = image::Plane<std::uint8_t>::allocate(budget, 1, 1).value();
    plane.view().row(0).front() = UINT8_MAX;
    const auto output = utf8_spelling(directory.path / "output");
    {
        const CheckpointStop stop{core::Checkpoint::commit, 0};
        const auto result = io::publish_png(output, plane.view().as_const(), budget,
                                            stop.cancellation(), cancel_after_gate);
        REQUIRE(!result);
        CHECK(result.error().code == core::ErrorCode::cancelled);
        CHECK(result.error().publication == core::Publication::not_published);
        CHECK(empty_directory(directory.path));
    }
    commit_source() = std::stop_source{};
    const core::Cancellation control{commit_source().get_token()};
    const auto result =
        io::publish_png(output, plane.view().as_const(), budget, control, cancel_after_gate);
    REQUIRE(result);
    CHECK(control.requested(core::Checkpoint::processing));
    CHECK(std::filesystem::is_regular_file(directory.path / "output" / "result.png"));
}
TEST_CASE("A late cancellation never erases refused or uncertain publication",
          "[cancellation][io]") {
    for (const auto commit : {refused_commit, ambiguous_commit, unclean_commit}) {
        const TemporaryDirectory directory{"docenhance-stop"};
        core::Budget budget{limit};
        auto plane = image::Plane<std::uint8_t>::allocate(budget, 1, 1).value();
        plane.view().row(0).front() = UINT8_MAX;
        commit_source() = std::stop_source{};
        const auto output = utf8_spelling(directory.path / "output");
        const auto result =
            io::publish_png(output, plane.view().as_const(), budget,
                            core::Cancellation{commit_source().get_token()}, commit);
        REQUIRE(!result);
        if (commit == refused_commit) {
            CHECK(result.error().code == core::ErrorCode::output);
            CHECK(result.error().publication == core::Publication::not_published);
            CHECK(empty_directory(directory.path / "output"));
        } else {
            CHECK(result.error().code == core::ErrorCode::publication_unknown);
            CHECK(result.error().publication == core::Publication::unknown);
        }
        if (commit == ambiguous_commit) {
            CHECK(std::filesystem::is_regular_file(directory.path / "output" / "result.png"));
        }
        if (commit == unclean_commit) {
            CHECK(std::filesystem::is_regular_file(directory.path / "output.staging-0" /
                                                   "foreign-entry"));
            CHECK(!std::filesystem::exists(directory.path / "output.staging-0" / "result.png"));
        }
    }
}
} // namespace docenhance::tests
