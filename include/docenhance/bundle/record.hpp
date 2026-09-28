// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/illumination.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace docenhance::bundle {
// What one run was given and what it produced, in types. Every field comes from a value the
// execution already held: this layer writes facts down, and defines no default, no option table
// and no processing rule of its own. A field that cannot be filled from a typed value is evidence
// that the type is missing a fact, and the type is what gets fixed.
//
// The record describes processing. It is finalized before the commit point, so it cannot assert
// that publication succeeded; publication state belongs to the command response.

inline constexpr unsigned record_version = 1;
inline constexpr const char* record_name = "run.json";
inline constexpr const char* image_name = "result.png";
inline constexpr const char* mask_name = "assets/protect-mask.png";

// The environment inputs a record needs, taken once in the composition root. Nothing below it
// reads a clock or an entropy source, so a test pins both and records stay reproducible.
struct RunContext {
    std::string identity; // 32 lowercase hexadecimal characters
    std::string recorded; // RFC 3339 instant in UTC
};

// A file this bundle carries, named relative to the bundle root.
using Artifact = core::NamedContent;

struct SourceFacts {
    core::ContentIdentity identity;
    // The name as supplied, without its directories. A basename can still carry personal
    // information, so a bundle is never described as anonymized.
    std::string name;
};

// White protects: the convention the protection decoder applies, recorded so a later reader does
// not have to guess which samples were excluded.
enum class MaskPolarity : std::uint8_t { white_protects };
// Masks are stated in the source's own orientation, after any recorded rotation was applied.
enum class MaskFrame : std::uint8_t { oriented_source };
struct ProtectionFacts {
    core::ContentIdentity original; // the mask file as it was supplied
    Artifact stored;                // the canonical mask this bundle carries
    std::uint32_t width{};
    std::uint32_t height{};
    MaskPolarity polarity = MaskPolarity::white_protects;
    MaskFrame frame = MaskFrame::oriented_source;
};

// What verification established, named rather than asserted: a bare true invites a reader to
// conclude more than a decode-back comparison can establish.
enum class Verification : std::uint8_t { not_performed, decoded_and_compared };

struct OutputFacts {
    Artifact artifact;
    image::RasterShape shape;
    bool profile_embedded = false;
    std::optional<image::Resolution> resolution;
    Verification verification = Verification::not_performed;
};

// The same variant the application admits; naming it here costs no second definition.
using Operation = std::variant<methods::Binarization, image::Continuous>;

struct RunRecord {
    RunContext context;
    core::BuildFacts build;
    SourceFacts source;
    Operation operation;
    bool protection_supplied = false;
    OutputFacts output;
    std::optional<ProtectionFacts> protection;
    std::optional<image::ConversionReport> conversion;
    methods::IlluminationReport illumination;
};

// The record as the bytes a bundle carries. Serialization is the last step before the inventory
// is validated, because the output digest it contains only exists once the image is written and
// verified. The record never contains its own digest: that is computed over these bytes.
[[nodiscard]] core::Result<std::string> serialize(const RunRecord& record);
} // namespace docenhance::bundle
