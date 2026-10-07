// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
// Property fuzzing of the run-record reader, which is the one place this program parses something
// somebody else wrote. Two questions: does an arbitrary document ever get past the bounds it
// claims to enforce, and does a record this program wrote survive being read back?
#include "docenhance/bundle/record.hpp"

#include "docenhance/bundle/inventory.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/illumination.hpp"
#include "support/entry_point.hpp"
#include "support/fuzz_input.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace {
using docenhance::fuzz::FuzzInput;
using docenhance::fuzz::require;
namespace bundle = docenhance::bundle;
namespace core = docenhance::core;
namespace image = docenhance::image;
namespace methods = docenhance::methods;

constexpr std::size_t max_document = bundle::record_max_bytes + 1;

std::span<const std::byte> as_bytes(std::string_view text) {
    // char is the narrow-character view of the same immutable bytes.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

bool hexadecimal(std::string_view text) {
    return text.size() == core::sha256_hex_length && std::ranges::all_of(text, [](char value) {
               return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
           });
}

// Where a declared name leads, resolved the way a reader of the bundle would: each step names one
// entry further down, so a step back up, a step that names nothing, and a name with no steps at
// all are the three ways out of a bundle. A dot pair inside a name, as in "run..json", is an
// ordinary character in an ordinary name and steps nowhere.
bool inside_bundle(std::string_view name) {
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

// Whatever a reader accepts, it must have checked. These are the claims the bundle layer makes
// about what it returns, restated where a fuzzer can attack them.
void accepted_records_are_usable(const bundle::DeclaredBundle& declared) {
    require(declared.version == bundle::record_version,
            "an accepted record is of a version this build supports");
    require(declared.source.decoding.has_value() == (declared.version == bundle::record_version),
            "current records carry required source decoding observations");
    if (declared.source.decoding) {
        require(image::valid_source_description(*declared.source.decoding),
                "accepted source alternatives have valid precision, components, framing and "
                "metadata relations");
    }
    require(declared.inventory.size() <= bundle::record_max_artifacts,
            "an accepted record declares no more artifacts than a bundle may hold");
    require(declared.operation.has_value(), "an accepted record retains its validated operation");
    require(declared.run.size() == 32, "an accepted record has a complete run identity");
    require(hexadecimal(declared.source.identity.sha256),
            "accepted source identity is well formed");
    require(hexadecimal(declared.build.dependency_lock_sha256),
            "accepted build identity is well formed");
    require(declared.source.identity.bytes != 0, "accepted source has a nonzero extent");
    require(declared.protection_supplied == declared.protection.has_value(),
            "protection presence agrees with request");
    require(declared.output.shape.width != 0 && declared.output.shape.height != 0,
            "accepted output has nonempty dimensions");
    require(declared.output.shape.depth == image::SampleDepth::byte() ||
                declared.output.shape.depth == image::SampleDepth::word(),
            "accepted output uses supported precision");
    require(declared.output.artifact.name == bundle::image_name,
            "accepted output has its unique role");
    if (declared.protection) {
        require(declared.inventory.size() == 2,
                "protection declares exactly one additional artifact");
        require(declared.protection->stored.name == bundle::mask_name,
                "accepted protection has its unique role");
        require(declared.protection->width == declared.output.shape.width &&
                    declared.protection->height == declared.output.shape.height,
                "accepted protection uses oriented output extents");
    } else {
        require(declared.inventory.size() == 1, "an unprotected bundle declares exactly its image");
    }
    for (const auto& artifact : declared.inventory) {
        require(!artifact.name.empty(), "an accepted artifact is named");
        require(artifact.name.front() != '/', "an accepted artifact is named relatively");
        require(inside_bundle(artifact.name), "an accepted artifact stays inside the bundle");
        require(!artifact.name.contains('\\'), "an accepted artifact uses one separator");
        require(hexadecimal(artifact.identity.sha256), "an accepted artifact carries a digest");
    }
}

// A record this program wrote, so the fuzzer can mutate something structurally valid rather than
// spending its budget rediscovering JSON.
std::string written_record(FuzzInput& input) {
    const auto threshold = input.unit();
    auto method = methods::FixedThreshold::create(threshold);
    if (!method) {
        method = methods::FixedThreshold::create(0.5);
    }
    const methods::IlluminationReport illumination;
    const bundle::RunRecord record{
        .context = {.identity = std::string(32, 'a'), .recorded = "2026-01-01T00:00:00Z"},
        .build = core::build_facts(),
        .source =
            {
                .identity = {.sha256 = std::string(core::sha256_hex_length, 'b'), .bytes = 1},
                .name = "page.png",
                .decoding = image::PngSource{.width = 1, .height = 1, .depth = 8, .color_type = 0},
            },
        .operation = methods::Binarization{*method},
        .protection_supplied = false,
        .output =
            {
                .artifact =
                    {
                        .name = bundle::image_name,
                        .identity =
                            {
                                .sha256 = std::string(core::sha256_hex_length, 'c'),
                                .bytes = 2,
                            },
                    },
                .shape =
                    {
                        .width = 1,
                        .height = 1,
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
    };
    auto written = bundle::serialize(record);
    require(written.has_value(), "a record this program built always serializes");
    return *written;
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    FuzzInput input(std::span<const std::uint8_t>(data, size));
    if ((input.byte() & 1U) == 0) {
        // Anything at all, including documents far past every bound.
        const auto document = input.rest(max_document);
        if (auto declared = bundle::read_record(as_bytes(document))) {
            accepted_records_are_usable(*declared);
        }
        return 0;
    }
    auto document = written_record(input);
    require(bundle::read_record(as_bytes(document)).has_value(),
            "a record this program wrote is one it can read");
    // One mutation at a time, so a rejection is attributable.
    if (!document.empty()) {
        const auto at = static_cast<std::size_t>(input.bounded(document.size() - 1));
        document.at(at) = static_cast<char>(input.byte());
    }
    if (auto declared = bundle::read_record(as_bytes(document))) {
        accepted_records_are_usable(*declared);
    }
    return 0;
}
