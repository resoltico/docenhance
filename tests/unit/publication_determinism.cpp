// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "bundle_verify.hpp"
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/color/profile.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/fs/artifact_limits.hpp"
#include "docenhance/fs/digest.hpp"
#include "docenhance/host/processor.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/source.hpp"
#include "file_contents.hpp"
#include "jpeg_fixture.hpp"
#include "png_fixture.hpp"
#include "temporary_directory.hpp"
#include "tiff_fixture.hpp"

#include <array>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace docenhance::tests {
namespace {
constexpr unsigned rgba_height = 449;
record::RunContext execution_context() {
    return {.identity = std::string(32, 'b'), .recorded = "2026-10-19T00:00:00Z"};
}
std::vector<std::uint8_t> rgba_icc_png() {
    core::Budget budget{core::default_memory_limit};
    const auto profile = color::make_output_profile(budget, false);
    REQUIRE(profile);
    REQUIRE(!profile.value().bytes().empty());
    REQUIRE(profile.value().bytes().size() <= 65535);
    std::vector<std::uint8_t> profile_bytes;
    for (const auto byte : profile.value().bytes()) {
        profile_bytes.push_back(std::to_integer<std::uint8_t>(byte));
    }
    std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<std::uint8_t> header;
    append_be32(header, 8);
    append_be32(header, rgba_height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    append_chunk(png, "IHDR", header);
    std::vector<std::uint8_t> iccp{'i', 'c', 'c', 0, 0};
    const auto compressed_profile = stored_deflate(profile_bytes);
    iccp.insert(iccp.end(), compressed_profile.begin(), compressed_profile.end());
    append_chunk(png, "iCCP", iccp);
    std::vector<std::uint8_t> raw;
    constexpr std::array<std::uint8_t, 3> alpha{0, 128, 255};
    for (unsigned y = 0; y < rgba_height; ++y) {
        raw.push_back(0);
        for (unsigned x = 0; x < 8; ++x) {
            raw.push_back(static_cast<std::uint8_t>(x * 31));
            raw.push_back(static_cast<std::uint8_t>(y * 29));
            raw.push_back(static_cast<std::uint8_t>((x + y) * 17));
            raw.push_back(alpha.at((x + y) % alpha.size()));
        }
    }
    append_chunk(png, "IDAT", stored_deflate(raw));
    append_chunk(png, "IEND", {});
    return png;
}
void check_publication_container(const record::RunRecord& record,
                                 const std::filesystem::path& input) {
    const auto& interpretation = record.execution.interpretation;
    if (input.extension() == ".jpg") {
        CHECK(std::holds_alternative<record::JpegContainer>(record.source.container));
        CHECK(interpretation.decoded.model == image::SampleModel::rgb);
    } else if (input.extension() == ".tif") {
        CHECK(std::holds_alternative<record::TiffContainer>(record.source.container));
        CHECK(interpretation.decoded.model == image::SampleModel::rgb);
    } else {
        CHECK(std::holds_alternative<record::PngContainer>(record.source.container));
    }
}
void check_publication_source(const record::RunRecord& record, const std::filesystem::path& input,
                              const core::ContentIdentity& source_identity) {
    CHECK(record.source.sha256 == source_identity.sha256);
    CHECK(record.source.bytes == source_identity.bytes);
    const auto& interpretation = record.execution.interpretation;
    check_publication_container(record, input);
    if (input.filename() == "rgba-icc.png") {
        CHECK(interpretation.decoded.model == image::SampleModel::rgba);
        CHECK(interpretation.decoded.height == rgba_height);
        CHECK(interpretation.interpretation == image::Interpretation::icc);
        CHECK(interpretation.flattened_pixels > 0);
    }
}
struct Setting {
    std::optional<unsigned> workers = std::nullopt;
    std::uint64_t memory_mib = 0;
};
struct PublicationBytes {
    std::string png, record;
};
app::ProcessRequest execution_request(const std::filesystem::path& input,
                                      const std::filesystem::path& out, const char* const mode) {
    app::Invocation invocation;
    invocation.command = app::Command::process;
    invocation.operands = {utf8_spelling(input)};
    invocation.option(app::OptionId::out_dir) = utf8_spelling(out);
    invocation.option(app::OptionId::output_mode) = mode;
    auto request = app::prepare_process(invocation);
    REQUIRE(request);
    return std::move(request).value();
}
void check_execution_diagnostics(const app::Diagnostics& diagnostics, const Setting& setting,
                                 const app::ExecutionControl& control) {
    CHECK(diagnostics.memory_limit_mib == setting.memory_mib);
    CHECK(diagnostics.threads >= 1);
    CHECK(diagnostics.threads <= setting.workers.value_or(256));
    if (setting.workers.has_value()) {
        CHECK(diagnostics.threads == setting.workers.value());
    }
    CHECK(diagnostics.charged_peak_bytes <= control.memory_limit());
}
PublicationBytes publication_bytes(const std::filesystem::path& directory) {
    auto png = file_contents(directory / "result.png");
    auto record = file_contents(directory / "run.json");
    REQUIRE(!png.empty());
    REQUIRE(!record.empty());
    return {.png = std::move(png), .record = std::move(record)};
}
void check_publication_bytes(const std::filesystem::path& directory,
                             const record::RunRecord& record, PublicationBytes& baseline,
                             bool first) {
    const auto bytes = publication_bytes(directory);
    if (first) {
        baseline = bytes;
    }
    CHECK(bytes.png == baseline.png);
    CHECK(bytes.record == baseline.record);
    const auto verified = host::verify_bundle(utf8_spelling(directory), {});
    REQUIRE(verified);
    CHECK(verified.value().declared == record);
}
std::size_t compare_mode_publications(const std::filesystem::path& input,
                                      const std::filesystem::path& parent, const char* const mode,
                                      const core::ContentIdentity& source_identity,
                                      host::Processor& processor) {
    std::size_t completed = 0;
    const std::array settings{
        Setting{.workers = 1, .memory_mib = 64},
        Setting{.workers = 2, .memory_mib = 1024},
        Setting{.workers = 7, .memory_mib = 4096},
        Setting{.workers = std::nullopt, .memory_mib = 64},
    };
    PublicationBytes baseline;
    for (std::size_t index = 0; index < settings.size(); ++index) {
        const auto directory = parent / (std::string{mode} + std::to_string(index));
        const auto request = execution_request(input, directory, mode);
        const auto& setting = settings.at(index);
        const auto control =
            app::ExecutionControl::create(setting.workers, setting.memory_mib * 1024 * 1024);
        REQUIRE(control);
        const auto published = processor.process(request, control.value());
        if (!published) {
            const auto& failure = published.error();
            UNSCOPED_INFO("publication input="
                          << utf8_spelling(input) << " output=" << utf8_spelling(directory)
                          << " mode=" << mode << " index=" << index << " workers="
                          << setting.workers.value_or(0) << " memory_mib=" << setting.memory_mib
                          << " code=" << failure.error.identifier()
                          << " message=" << failure.error.message
                          << " publication=" << static_cast<int>(failure.publication));
        }
        REQUIRE(published);
        check_publication_source(published.value().record, input, source_identity);
        check_execution_diagnostics(published.value().diagnostics, setting, control.value());
        check_publication_bytes(directory, published.value().record, baseline, index == 0);
        ++completed;
    }
    return completed;
}
std::size_t compare_execution_publications(const std::filesystem::path& input,
                                           const std::filesystem::path& output_parent,
                                           const core::ContentIdentity& source_identity) {
    host::Processor processor{execution_context};
    std::size_t completed = 0;
    for (const auto* const mode : {"preserve", "gray", "bw"}) {
        completed +=
            compare_mode_publications(input, output_parent, mode, source_identity, processor);
    }
    return completed;
}
} // namespace
TEST_CASE("Worker and budget controls preserve complete publication bytes and fixed verification") {
    static_assert(fs::bundle_snapshot_budget == std::size_t{1024} * 1024 * 1024);
    const TemporaryDirectory temporary{"docenhance-execution-control"};
    struct PublicationInput {
        const char* name;
        std::vector<std::uint8_t> bytes;
    };
    const std::array sources{
        PublicationInput{
            .name = "source.png",
            .bytes = make_gray_png({
                .width = 8,
                .height = 8,
                .depth = 8,
                .interlaced = false,
                .samples = std::vector<std::uint8_t>(64, 127),
            }),
        },
        PublicationInput{.name = "rgba-icc.png", .bytes = rgba_icc_png()},
        PublicationInput{.name = "source.jpg", .bytes = jpeg_fixture("rgb-1x1-baseline.jpg")},
        PublicationInput{.name = "source.tif", .bytes = tiff_fixture("d16-c3-t0-p2-b1-l0.tif")},
    };
    std::size_t completed = 0;
    for (const auto& source : sources) {
        REQUIRE(!source.bytes.empty());
        const auto input = temporary.path / source.name;
        {
            std::ofstream stream{input, std::ios::binary};
            for (const auto byte : source.bytes) {
                stream.put(static_cast<char>(byte));
            }
            stream.flush();
            REQUIRE(stream.good());
        }
        const auto identity = fs::identify(std::as_bytes(std::span{source.bytes}));
        REQUIRE(identity);
        const auto output_parent = temporary.path / (std::string{source.name} + "-outputs");
        REQUIRE(std::filesystem::create_directory(output_parent));
        completed += compare_execution_publications(input, output_parent, identity.value());
    }
    REQUIRE(completed == 48);
}
} // namespace docenhance::tests
