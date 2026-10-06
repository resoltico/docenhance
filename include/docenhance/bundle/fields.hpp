// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/identity.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace docenhance::bundle {
[[nodiscard]] nlohmann::ordered_json tvl1_fields(const methods::Tvl1Report& report);
[[nodiscard]] nlohmann::ordered_json denoising_fields(const methods::DenoisingReport& report);
[[nodiscard]] nlohmann::ordered_json
denoising_request_fields(const methods::DenoisingReport& report);
// One mapping from typed execution facts to their written form. The persistent record and the
// command response both use it, so a fact cannot be spelled one way on disk and another on
// stdout, and a field cannot be added to one and forgotten in the other.
[[nodiscard]] std::string_view status_name(methods::IlluminationStatus status) noexcept;
[[nodiscard]] std::string_view reason_name(methods::IlluminationReason reason) noexcept;
[[nodiscard]] nlohmann::ordered_json
morphology_parameters_fields(const methods::MorphologyParameters& parameters);
[[nodiscard]] nlohmann::ordered_json
morphology_fields(const methods::MorphologyMeasurements& measurements);
[[nodiscard]] nlohmann::ordered_json illumination_fields(const methods::IlluminationReport& report);
[[nodiscard]] nlohmann::ordered_json conversion_fields(const image::ConversionReport& report);
[[nodiscard]] nlohmann::ordered_json source_fields(const image::SourceDescription& description);
// What identifies a published bundle: the run, and the digest of the record it carries. The record
// on disk asserts nothing about publication, so these are what a later reconciliation matches.
[[nodiscard]] nlohmann::ordered_json record_fields(const std::string& run,
                                                   const core::ContentIdentity& record);
// The warning codes a conversion earned, in the order they are always reported.
[[nodiscard]] std::vector<std::string_view>
conversion_warnings(const image::ConversionReport& report);
[[nodiscard]] nlohmann::ordered_json contrast_fields(const methods::ContrastReport& report);
[[nodiscard]] nlohmann::ordered_json contrast_request_fields(const methods::ContrastReport& report);
} // namespace docenhance::bundle
