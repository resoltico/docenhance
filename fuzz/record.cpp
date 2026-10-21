// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/record/record.hpp"

#include "docenhance/core/bundle_path.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binary_facts.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/restoration_facts.hpp"
#include "docenhance/record/inventory.hpp"
#include "docenhance/record/record_version.hpp"
#include "docenhance/record/request.hpp"
#include "docenhance/record/source.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
namespace {
using docenhance::fuzz::FuzzInput;
using docenhance::fuzz::require;
namespace records = docenhance::record;
namespace core = docenhance::core;
namespace image = docenhance::image;
namespace methods = docenhance::methods;
constexpr std::size_t max_document = records::record_max_bytes + 1;
std::span<const std::byte> as_bytes(std::string_view text) {
    // char is the narrow-character view of the same immutable bytes.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}
bool hexadecimal(std::string_view text, std::size_t length = core::sha256_hex_length) {
    return text.size() == length && std::ranges::all_of(text, [](char value) {
               return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
           });
}
bool inside_record(std::string_view name) {
    std::size_t steps = 0;
    for (const auto part : std::views::split(name, '/')) {
        const std::string_view step{part};
        if (step.empty() || step == "." || step == "..") {
            return false;
        }
        ++steps;
    }
    return steps > 0;
}
template <typename Selection, typename Facts>
void paired(const std::optional<Selection>& selection, const std::optional<Facts>& facts) {
    require(selection.has_value() == facts.has_value(), "selection and completed facts agree");
    if constexpr (requires {
                      selection->index();
                      facts->index();
                  }) {
        if (selection) {
            if (!facts.has_value()) {
                docenhance::fuzz::fail("selection and completed facts agree");
            }
            require(selection->index() == facts->index(), "same method alternative");
        }
    }
}
void usable_assets(const records::RunRecord& value) {
    const auto inventory = records::inventory(value);
    require(inventory.has_value(), "accepted record has a usable inventory");
    require(inventory->size() == value.assets.size() + 1 &&
                inventory->size() <= records::record_max_artifacts,
            "closed artifact cardinality");
    unsigned masks = 0;
    unsigned kernels = 0;
    for (const auto& asset : value.assets) {
        if (const auto* const mask = std::get_if<records::ProtectionAsset>(&asset)) {
            ++masks;
            const auto oriented = image::oriented_shape(value.execution.interpretation.decoded,
                                                        value.execution.interpretation.orientation);
            require(mask->path == records::MaskPath::canonical &&
                        mask->frame == records::MaskFrame::oriented_source &&
                        mask->width == oriented.width && mask->height == oriented.height,
                    "mask retains source oriented frame B");
            require(mask->protected_samples <= std::uint64_t{mask->width} * mask->height,
                    "protected count fits the mask");
        } else {
            ++kernels;
            require(std::get<records::PsfAsset>(asset).path == records::PsfPath::canonical &&
                        std::get<records::PsfAsset>(asset).bytes <= core::psf_encoded_bytes_max,
                    "PSF asset has its tighter encoded bound");
        }
    }
    if (value.request.protection) {
        const auto& supplied = value.request.protection->supplied;
        require(hexadecimal(supplied.sha256) && supplied.bytes != 0 &&
                    supplied.bytes <= core::source_encoded_bytes_max,
                "bounded supplied mask");
    }
    const bool kernel =
        value.request.restoration &&
        std::holds_alternative<methods::Kernel>(value.request.restoration.value().parameters.psf);
    require(masks == static_cast<unsigned>(value.request.protection.has_value()) &&
                kernels == static_cast<unsigned>(kernel),
            "each selected resource has exactly one confined asset");
    for (const auto& artifact : *inventory) {
        require(!artifact.name.empty() && artifact.name.front() != '/' &&
                    inside_record(artifact.name),
                "accepted artifact stays inside its bundle");
        require(artifact.name.size() <= core::bundle_path_max_bytes &&
                    core::valid_utf8(artifact.name) && !artifact.name.contains('\\') &&
                    !artifact.name.contains(':') && !artifact.name.contains('\0'),
                "portable path bytes are never repaired");
        require(hexadecimal(artifact.identity.sha256) && artifact.identity.bytes != 0 &&
                    artifact.identity.bytes <= core::bundle_max_file_bytes,
                "finite artifact identity");
        require(std::ranges::count(*inventory, artifact.name, &records::Artifact::name) == 1,
                "each canonical role appears once");
    }
}
void accepted_records_are_usable(const records::RunRecord& value) {
    require(value.context.version == records::record_version &&
                hexadecimal(value.context.identity, core::run_identity_hex_length) &&
                core::valid_instant(value.context.recorded),
            "current complete run identity");
    const auto source = records::decoded_source(value.source.container);
    require(source && image::valid_source_facts(*source), "admitted native source facts");
    require(hexadecimal(value.source.sha256) && value.source.bytes != 0 &&
                value.source.bytes <= core::source_encoded_bytes_max,
            "finite encoded source identity");
    require(hexadecimal(value.build.dependency_lock_sha256), "well formed build identity");
    require(!value.source.name.empty() && !value.source.name.contains('/') &&
                !value.source.name.contains('\0') && core::valid_utf8(value.source.name),
            "source basename identity");
    const auto& report = value.execution.interpretation;
    require(image::validate(report) && value.output.shape() == report.output &&
                value.output.resolution == report.resolution &&
                value.request.rotation == report.rotation,
            "actual interpretation and output geometry agree");
    const auto frame = image::rotated_shape(
        image::oriented_shape(report.decoded, report.orientation), report.rotation);
    require(frame.width == value.output.width && frame.height == value.output.height,
            "G01 then G02 defines output frame C");
    const auto pixels = std::uint64_t{frame.width} * frame.height;
    const bool binary = std::holds_alternative<records::BinaryOutputRequest>(value.request.output);
    const auto bits = value.output.depth.bits();
    require((bits == 1 || bits == 8 || bits == 16) && (bits == 1) == binary &&
                value.output.profile_embedded == !binary,
            "binary precision and profile roles agree");
    require((value.output.channels == image::SampleModel::gray ||
             value.output.channels == image::SampleModel::rgb) &&
                value.output.path == records::OutputPath::canonical && value.output.width != 0 &&
                value.output.height != 0 && pixels <= image::source_pixels_max,
            "unique bounded output role");
    std::visit(
        [&](const auto& output) {
            require(image::valid_interpretation_policy(
                        {.alpha = output.alpha, .profile = output.profile_policy, .rotation = {}}),
                    "admitted interpretation policies");
            require((output.profile_policy == image::ProfilePolicy::srgb) ==
                        (report.interpretation == image::Interpretation::overridden_srgb),
                    "profile override agrees with observation");
            require(output.alpha != image::AlphaPolicy::reject || report.flattened_pixels == 0,
                    "reject-alpha never reports compositing");
        },
        value.request.output);
    paired(value.request.illumination, value.execution.illumination);
    paired(value.request.denoising, value.execution.denoising);
    paired(value.request.restoration, value.execution.restoration);
    paired(value.request.contrast, value.execution.contrast);
    paired(value.request.sharpening, value.execution.sharpening);
    paired(value.request.binarization, value.execution.binarization);
    if (value.execution.binarization) {
        std::visit(
            [&](const auto& facts) {
                require(facts.black_samples + facts.white_samples == pixels,
                        "every final pixel is classified");
            },
            value.execution.binarization.value());
    }
    usable_assets(value);
}
image::SourceFacts synthetic_source(FuzzInput& input, image::Extent extent) {
    switch (input.byte() % 3) {
    case 0: {
        constexpr std::array precision{1U, 2U, 4U, 8U, 16U};
        return image::PngFacts{
            .width = extent.width,
            .height = extent.height,
            .depth = precision.at(input.byte() % precision.size()),
        };
    }
    case 1:
        return image::JpegFacts{
            .width = extent.width,
            .height = extent.height,
            .scans = 1,
            .component_ids = {1, 0, 0},
            .adobe_transform = std::nullopt,
            .jfif_resolution = std::nullopt,
            .exif_resolution = std::nullopt,
        };
    default:
        return image::TiffFacts{
            .width = extent.width,
            .height = extent.height,
            .orientation = {},
            .resolution = std::nullopt,
        };
    }
}
void synthetic_assets(records::RunRecord& value, bool mask, bool kernel) {
    const auto& report = value.execution.interpretation;
    const auto oriented = image::oriented_shape(report.decoded, report.orientation);
    if (mask) {
        value.assets.emplace_back(records::ProtectionAsset{
            .sha256 = std::string(64, 'd'),
            .bytes = 1,
            .width = oriented.width,
            .height = oriented.height,
            .protected_samples = 1,
        });
    }
    if (kernel) {
        value.assets.emplace_back(records::PsfAsset{.sha256 = std::string(64, 'e'), .bytes = 1});
        value.execution.restoration = methods::WienerFacts{
            .reason = methods::WienerReason::zero_blend,
            .counts =
                {
                    .eligible = (std::uint64_t{oriented.width} * oriented.height) -
                                static_cast<unsigned>(mask),
                    .protected_samples = static_cast<unsigned>(mask),
                },
            .psf =
                {
                    .kind = methods::PsfKind::kernel,
                    .width = 3,
                    .height = 3,
                    .normalized_sum = 1,
                },
        };
    }
}
void synthetic_execution(records::RunRecord& value, image::Extent extent, unsigned depth,
                         bool binary) {
    value.execution.interpretation = {
        .decoded =
            {
                .width = extent.width,
                .height = extent.height,
                .depth = depth == 16 ? image::SampleDepth::word() : image::SampleDepth::byte(),
            },
        .output = value.output.shape(),
        .orientation = {},
        .rotation = {},
        .resolution = std::nullopt,
        .assumed_transfer = true,
    };
    if (binary) {
        methods::FixedFacts facts;
        facts.black_samples = std::uint64_t{extent.width} * extent.height;
        value.execution.binarization = facts;
    }
}
std::string written_record(FuzzInput& input) {
    const auto method = methods::FixedThreshold::create(input.unit());
    require(method.has_value(), "finite unit threshold is admitted without substitution");
    const image::Extent extent{
        .width = 1U + (input.byte() % 16U),
        .height = 1U + (input.byte() % 16U),
    };
    const auto source = synthetic_source(input, extent);
    auto container = records::source_container(source);
    require(container.has_value(), "synthetic native source facts are admitted");
    const auto flags = input.byte();
    const bool binary = (flags & 1U) != 0;
    const bool mask = (flags & 2U) != 0;
    const bool kernel = (flags & 4U) != 0;
    methods::Plan plan;
    plan.protection = mask;
    if (binary) {
        plan.output = methods::BinaryOutput{.method = *method};
    }
    const auto wiener = methods::Wiener::create({.psf = methods::Kernel{}, .blend = 0});
    require(wiener.has_value(), "synthetic bypass still has an admitted kernel selection");
    if (kernel) {
        plan.photometric.restoration = *wiener;
    }
    std::optional<core::ContentIdentity> supplied;
    if (mask) {
        supplied = core::ContentIdentity{.sha256 = std::string(64, 'f'), .bytes = 1};
    }
    auto request = records::requested(plan, std::move(supplied));
    require(request.has_value(), "persistent resource selection is admitted");
    const auto build = core::build_facts();
    unsigned depth = 8;
    if (const auto* const png = std::get_if<image::PngFacts>(&source)) {
        depth = png->depth == 16 ? 16 : 8;
    }
    const auto png_depth = image::PngDepth::from_bits(binary ? 1 : depth);
    require(png_depth.has_value(), "synthetic actual output precision is admitted");
    if (!png_depth.has_value()) {
        docenhance::fuzz::fail("synthetic actual output precision is admitted");
    }
    records::RunRecord value;
    value.context = {.identity = std::string(32, 'a'), .recorded = "2026-01-01T00:00:00Z"};
    value.build = {
        .version = std::string{build.version},
        .platform = std::string{build.platform},
        .compiler = std::string{build.compiler},
        .dependency_lock_sha256 = std::string{build.dependency_lock_sha256},
    };
    value.source = {
        .name = "page.png",
        .sha256 = std::string(64, 'b'),
        .bytes = 1,
        .container = std::move(*container),
    };
    value.request = std::move(*request);
    value.output = {
        .sha256 = std::string(64, 'c'),
        .bytes = 2,
        .width = extent.width,
        .height = extent.height,
        .depth = *png_depth,
        .profile_embedded = !binary,
        .resolution = std::nullopt,
    };
    synthetic_execution(value, extent, depth, binary);
    synthetic_assets(value, mask, kernel);
    auto written = records::serialize(value);
    require(written.has_value(), "a synthetic typed record always serializes");
    const auto decoded = records::read_record(as_bytes(*written));
    require(decoded.has_value() && *decoded == value, "typed record round trip");
    accepted_records_are_usable(*decoded);
    return std::move(*written);
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    FuzzInput input(std::span<const std::uint8_t>(data, size));
    if ((input.byte() & 1U) == 0) {
        const auto document = input.rest(max_document);
        if (auto declared = records::read_record(as_bytes(document))) {
            require(document.size() <= records::record_max_bytes, "reader byte ceiling");
            accepted_records_are_usable(*declared);
        }
        return 0;
    }
    auto document = written_record(input);
    require(records::read_record(as_bytes(document)).has_value(), "writer bytes remain readable");
    if (!document.empty()) {
        const auto at = static_cast<std::size_t>(input.bounded(document.size() - 1));
        document.at(at) = static_cast<char>(input.byte());
    }
    if (auto declared = records::read_record(as_bytes(document))) {
        accepted_records_are_usable(*declared);
    }
    return 0;
}
