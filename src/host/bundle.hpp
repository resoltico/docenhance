// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/bundle/record.hpp"
#include "docenhance/color/converter.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace docenhance::host {
// Publishing a run: the image, the mask that was in force and the record describing both, written
// as one inventory and committed together. The record comes last because it carries their digests.

// The mask actually applied, and the identity of the file it was read from. The stored asset is
// the canonical plane that excluded samples, which may differ in depth from the file supplied.
struct MaskFacts {
    core::ContentIdentity supplied;
    image::PlaneView<const std::uint8_t> canonical;
};

// Representation selects its matching operation and observation owners together.
struct BinaryArtwork {
    image::PlaneView<const std::uint8_t> samples;
    methods::Binarization method;
};
struct ContinuousArtwork {
    std::reference_wrapper<image::RowSource> rows;
    image::Continuous operation;
    std::reference_wrapper<const color::Converter> converter;
    std::optional<MaskFacts> mask;
    std::reference_wrapper<const methods::IlluminationReport> illumination;
    std::reference_wrapper<const methods::DenoisingReport> denoising;
};
using Artwork = std::variant<BinaryArtwork, ContinuousArtwork>;
struct RunPublication {
    std::reference_wrapper<const std::string> output_directory;
    Artwork artwork;
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const core::Cancellation> cancellation;
    std::reference_wrapper<const bundle::RunContext> context;
    core::ContentIdentity source;
    std::string source_name;
    std::optional<image::SourceDescription> source_decoding = std::nullopt;
};

// What was published, and what the response needs to identify it.
struct PublishedRun {
    std::string output;
    std::string run;
    core::ContentIdentity record;
    bundle::Verification verification = bundle::Verification::not_performed;
    // What the conversion observed while the image was produced, as the record states it.
    std::optional<image::ConversionReport> conversion;
};

[[nodiscard]] core::Result<PublishedRun> publish_run(const RunPublication& run);
} // namespace docenhance::host
