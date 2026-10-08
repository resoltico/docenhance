// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/bundle/record.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
namespace docenhance::bundle {
struct DeclaredBundle;
}
namespace docenhance::bundle {
using RecordJson = nlohmann::json;
[[nodiscard]] core::Result<RecordJson> parse_record(std::string_view text);
// Missing/type errors stay inside the reader's JSON exception boundary. Numeric conversion
// always follows an explicit domain check; the JSON library does not check narrowing.
[[nodiscard]] const RecordJson& record_field(const RecordJson& object, std::string_view name);
[[nodiscard]] std::uint64_t record_integer(const RecordJson& value, std::uint64_t maximum);
[[nodiscard]] double record_number(const RecordJson& value);
[[nodiscard]] bool record_boolean(const RecordJson& value);
[[nodiscard]] std::string record_text(const RecordJson& value);
[[nodiscard]] core::Result<image::RasterShape> record_shape(const RecordJson& value);
[[nodiscard]] std::optional<image::Resolution> record_resolution(const RecordJson& value);
[[nodiscard]] core::ContentIdentity record_identity(const RecordJson& value);
[[nodiscard]] core::Result<Operation> record_operation(const RecordJson& value);
[[nodiscard]] core::Result<image::ConversionReport> record_conversion(const RecordJson& value);
[[nodiscard]] core::Result<methods::IlluminationReport>
record_illumination(const RecordJson& value);
[[nodiscard]] core::Result<methods::DenoisingReport> record_denoising(const RecordJson& value);
[[nodiscard]] core::Result<void>
validate_denoising_claims(const RecordJson& document, DeclaredBundle& d, const Operation& operation,
                          const methods::IlluminationReport& light);
[[nodiscard]] bool illumination_agrees(const DeclaredBundle& d);
[[nodiscard]] core::Result<void> validate_record_claims(const RecordJson& document,
                                                        DeclaredBundle& d);
[[nodiscard]] core::Result<image::SourceDescription> record_source(const RecordJson& value);
[[nodiscard]] core::Result<SourceFacts> record_source_facts(const RecordJson& value);
[[nodiscard]] bool source_agrees(const DeclaredBundle& declared);
[[nodiscard]] core::Result<void> validate_contrast_claims(const RecordJson& document,
                                                          DeclaredBundle& d,
                                                          const Operation& operation,
                                                          const methods::IlluminationReport& light);
[[nodiscard]] core::Result<void>
validate_restoration_claims(const RecordJson& document, DeclaredBundle& d,
                            const Operation& operation, const methods::IlluminationReport& light);
[[nodiscard]] core::Result<void> validate_sharpen_claims(const RecordJson& document,
                                                         DeclaredBundle& d,
                                                         const Operation& operation,
                                                         const methods::IlluminationReport& light);
} // namespace docenhance::bundle
