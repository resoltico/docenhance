// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/record/record_version.hpp"
#include "docenhance/record/request.hpp"
#include "docenhance/record/request_fields.hpp"
#include "docenhance/record/source.hpp"
#include "docenhance/record/source_fields.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
namespace docenhance::record {
inline constexpr std::size_t record_max_bytes = std::size_t{256} * 1024;
inline constexpr std::size_t record_max_depth = 16;
inline constexpr std::size_t record_max_events = 2048;
inline constexpr const char* record_name = "run.json";
inline constexpr const char* image_name = "result.png";
inline constexpr const char* mask_name = "assets/protect-mask.png";
inline constexpr const char* psf_name = "assets/psf.png";
// Environment values are captured once by the host; record reading never consults the clock.
struct RunContext {
    unsigned version = record_version;
    std::string identity;
    std::string recorded;
    bool operator==(const RunContext&) const = default;
};
// Untrusted wire text owns storage; static borrowed BuildFacts remain a separate core value.
struct RecordedBuild {
    std::string version;
    std::string platform;
    std::string compiler;
    std::string dependency_lock_sha256;
    bool operator==(const RecordedBuild&) const = default;
};
using Artifact = core::NamedContent;
struct SourceFacts {
    std::string name;
    std::string sha256;
    std::uint64_t bytes{};
    SourceContainer container;
    bool operator==(const SourceFacts&) const = default;
};
enum class MaskFrame : std::uint8_t { oriented_source };
enum class MaskPath : std::uint8_t { canonical };
enum class PsfPath : std::uint8_t { canonical };
enum class OutputPath : std::uint8_t { canonical };
struct ProtectionAsset {
    MaskPath path = MaskPath::canonical;
    std::string sha256;
    std::uint64_t bytes{};
    std::uint32_t width{};
    std::uint32_t height{};
    MaskFrame frame = MaskFrame::oriented_source;
    std::uint64_t protected_samples{};
    bool operator==(const ProtectionAsset&) const = default;
};
struct PsfAsset {
    PsfPath path = PsfPath::canonical;
    std::string sha256;
    std::uint64_t bytes{};
    bool operator==(const PsfAsset&) const = default;
};
using Asset = std::variant<ProtectionAsset, PsfAsset>;
enum class Verification : std::uint8_t { decoded_and_compared };
struct OutputFacts {
    OutputPath path = OutputPath::canonical;
    std::string sha256;
    std::uint64_t bytes{};
    std::uint32_t width{};
    std::uint32_t height{};
    image::SampleModel channels = image::SampleModel::gray;
    image::PngDepth depth;
    bool profile_embedded = false;
    std::optional<image::Resolution> resolution;
    Verification verification = Verification::decoded_and_compared;
    [[nodiscard]] image::PngShape shape() const noexcept {
        return {.width = width, .height = height, .model = channels, .depth = depth};
    }
    bool operator==(const OutputFacts&) const = default;
};
struct Execution : methods::StageReports {
    image::InterpretationReport interpretation;
    bool operator==(const Execution&) const = default;
};
// Finalized before commit: publication and this record's digest belong to the response only.
struct RunRecord {
    RunContext context;
    RecordedBuild build;
    SourceFacts source;
    Request request;
    Execution execution;
    std::vector<Asset> assets;
    OutputFacts output;
    bool operator==(const RunRecord&) const = default;
};
[[nodiscard]] bool validate(const RunContext& value);
[[nodiscard]] bool validate(const RecordedBuild& value);
[[nodiscard]] bool validate(const SourceFacts& value);
[[nodiscard]] bool validate(const ProtectionAsset& value);
[[nodiscard]] bool validate(const PsfAsset& value);
[[nodiscard]] bool validate(const OutputFacts& value);
[[nodiscard]] bool validate(const Execution& value) noexcept;
[[nodiscard]] bool validate(const RunRecord& value);
[[nodiscard]] core::Result<std::string> serialize(const RunRecord& value);
} // namespace docenhance::record

namespace docenhance {
template <> struct core::WireTag<record::RunContext> {
    static constexpr std::string_view key = "format";
    static constexpr std::string_view value = "docenhance.run";
};
template <> struct core::Fields<record::RunContext> {
    using Value = record::RunContext;
    static constexpr auto list = std::tuple{
        core::field("version", &Value::version, core::Exact{record::record_version}),
        core::field("run", &Value::identity, core::Hex{.digits = core::run_identity_hex_length}),
        core::field("recorded", &Value::recorded, core::Instant{}),
    };
};
template <> struct core::Fields<record::RecordedBuild> {
    using Value = record::RecordedBuild;
    static constexpr auto list = std::tuple{
        core::field("application_version", &Value::version, core::Text{}),
        core::field("platform", &Value::platform, core::Text{}),
        core::field("compiler", &Value::compiler, core::Text{}),
        core::field("dependency_lock_sha256", &Value::dependency_lock_sha256,
                    core::Hex{.digits = core::sha256_hex_length}),
    };
};
template <> struct core::Fields<record::SourceFacts> {
    using Value = record::SourceFacts;
    static constexpr auto list = std::tuple{
        core::field("name", &Value::name, core::Text{}),
        core::field("sha256", &Value::sha256, core::Hex{.digits = core::sha256_hex_length}),
        core::field(
            "bytes", &Value::bytes,
            core::Range<std::uint64_t>{.minimum = 1, .maximum = core::source_encoded_bytes_max}),
        core::field("container", &Value::container),
    };
};
template <> struct core::EnumNames<record::MaskFrame> {
    static constexpr auto list = std::to_array({
        std::pair{record::MaskFrame::oriented_source, std::string_view{"metadata_oriented_source"}},
    });
};
template <> struct core::EnumNames<record::MaskPath> {
    static constexpr auto list = std::to_array({
        std::pair{record::MaskPath::canonical, std::string_view{record::mask_name}},
    });
};
template <> struct core::EnumNames<record::PsfPath> {
    static constexpr auto list = std::to_array({
        std::pair{record::PsfPath::canonical, std::string_view{record::psf_name}},
    });
};
template <> struct core::EnumNames<record::OutputPath> {
    static constexpr auto list = std::to_array({
        std::pair{record::OutputPath::canonical, std::string_view{record::image_name}},
    });
};
template <> struct core::EnumNames<record::Verification> {
    static constexpr auto list = std::to_array({
        std::pair{record::Verification::decoded_and_compared,
                  std::string_view{"decoded_and_compared"}},
    });
};
template <> struct core::WireTag<record::ProtectionAsset> {
    static constexpr std::string_view key = "role";
    static constexpr std::string_view value = "protection_mask";
};
template <> struct core::WireTag<record::PsfAsset> {
    static constexpr std::string_view key = "role";
    static constexpr std::string_view value = "psf_kernel";
};
template <> struct core::Fields<record::ProtectionAsset> {
    using Value = record::ProtectionAsset;
    static constexpr auto list = std::tuple{
        core::field("path", &Value::path),
        core::field("sha256", &Value::sha256, core::Hex{.digits = core::sha256_hex_length}),
        core::field(
            "bytes", &Value::bytes,
            core::Range<std::uint64_t>{.minimum = 1, .maximum = core::bundle_max_file_bytes}),
        core::field("width", &Value::width, core::Count{}),
        core::field("height", &Value::height, core::Count{}),
        core::field("frame", &Value::frame),
        core::field("protected_samples", &Value::protected_samples, core::Count{}),
    };
};
template <> struct core::Fields<record::PsfAsset> {
    using Value = record::PsfAsset;
    static constexpr auto list = std::tuple{
        core::field("path", &Value::path),
        core::field("sha256", &Value::sha256, core::Hex{.digits = core::sha256_hex_length}),
        core::field(
            "bytes", &Value::bytes,
            core::Range<std::uint64_t>{.minimum = 1, .maximum = core::psf_encoded_bytes_max}),
    };
};
template <> struct core::Fields<record::OutputFacts> {
    using Value = record::OutputFacts;
    static constexpr auto list = std::tuple{
        core::field("path", &Value::path),
        core::field("sha256", &Value::sha256, core::Hex{.digits = core::sha256_hex_length}),
        core::field(
            "bytes", &Value::bytes,
            core::Range<std::uint64_t>{.minimum = 1, .maximum = core::bundle_max_file_bytes}),
        core::field("width", &Value::width, core::Count{}),
        core::field("height", &Value::height, core::Count{}),
        core::field("channels", &Value::channels,
                    core::Choices{std::array{image::SampleModel::gray, image::SampleModel::rgb}}),
        core::field("depth", &Value::depth),
        core::field("profile_embedded", &Value::profile_embedded),
        core::field("resolution", &Value::resolution),
        core::field("verification", &Value::verification),
    };
};
template <> struct core::Fields<record::Execution> {
    static constexpr auto list = std::tuple_cat(
        std::tuple{core::field("interpretation", &record::Execution::interpretation)},
        core::Fields<methods::StageReports>::list);
};
template <> struct core::Fields<record::RunRecord> {
    using Value = record::RunRecord;
    static constexpr auto list = std::tuple{
        core::field("record", &Value::context),
        core::field("build", &Value::build),
        core::field("source", &Value::source),
        core::field("request", &Value::request),
        core::field("execution", &Value::execution),
        core::field("assets", &Value::assets, core::Range<std::size_t>{.minimum = 0, .maximum = 2}),
        core::field("output", &Value::output),
    };
};
} // namespace docenhance
