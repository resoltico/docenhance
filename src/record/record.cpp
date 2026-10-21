// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/record/record.hpp"

#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/record/record_version.hpp"
#include "docenhance/record/request.hpp"
#include "docenhance/record/source.hpp"
#include "validation.hpp"

#include <cstdint>
#include <string_view>
namespace docenhance::record {
namespace {
bool identity(std::string_view digest, std::uint64_t bytes, std::uint64_t limit) {
    return bytes != 0 && bytes <= limit && core::valid_hexadecimal(digest, core::sha256_hex_length);
}
bool text(std::string_view value) noexcept {
    return !value.empty() && !value.contains('\0') && core::valid_utf8(value);
}
} // namespace
bool validate(const RunContext& value) {
    return value.version == record_version &&
           core::valid_hexadecimal(value.identity, core::run_identity_hex_length) &&
           core::valid_instant(value.recorded);
}
bool validate(const RecordedBuild& value) {
    return text(value.version) && text(value.platform) && text(value.compiler) &&
           core::valid_hexadecimal(value.dependency_lock_sha256, core::sha256_hex_length);
}
bool validate(const SourceFacts& value) {
    return text(value.name) && !value.name.contains('/') &&
           identity(value.sha256, value.bytes, core::source_encoded_bytes_max) &&
           decoded_source(value.container).has_value();
}
bool validate(const ProtectionAsset& value) {
    const auto pixels = std::uint64_t{value.width} * value.height;
    return value.path == MaskPath::canonical && value.frame == MaskFrame::oriented_source &&
           identity(value.sha256, value.bytes, core::bundle_max_file_bytes) && value.width != 0 &&
           value.height != 0 && pixels <= image::source_pixels_max &&
           value.protected_samples <= pixels;
}
bool validate(const PsfAsset& value) {
    return value.path == PsfPath::canonical &&
           identity(value.sha256, value.bytes, core::psf_encoded_bytes_max);
}
bool validate(const OutputFacts& value) {
    return value.path == OutputPath::canonical &&
           identity(value.sha256, value.bytes, core::bundle_max_file_bytes) &&
           image::validate(value.shape()) && value.profile_embedded == (value.depth.bits() != 1) &&
           (!value.resolution || (value.resolution->x != 0 && value.resolution->y != 0)) &&
           value.verification == Verification::decoded_and_compared;
}
bool validate(const Execution& value) noexcept {
    return image::validate(value.interpretation) &&
           methods::validate(static_cast<const methods::StageReports&>(value));
}
bool validate(const RunRecord& value) {
    return validate(value.context) && validate(value.build) && validate(value.source) &&
           validate(value.request) && validate(value.execution) && validate(value.output) &&
           value.assets.size() <= 2 && source_agrees(value) && assets_agree(value) &&
           execution_agrees(value);
}

} // namespace docenhance::record
