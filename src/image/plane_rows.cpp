// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>

namespace docenhance::image {
OutputDescriptor PlaneRows::descriptor() const noexcept {
    return {
        .shape =
            {
                .width = image_.width(),
                .height = image_.height(),
                .model = SampleModel::gray,
                .depth = byte_bits,
            },
        .profile = {},
        .resolution = std::nullopt,
    };
}
core::Result<void> PlaneRows::row(std::uint32_t index, std::span<std::uint8_t> bytes,
                                  RowUse /*use*/) {
    if (image_.empty() || index >= image_.height() || bytes.size() != image_.width()) {
        return core::failure(core::ErrorCode::argument, "Invalid binary output row range");
    }
    const auto samples = image_.row(index);
    if (bytes.size() != samples.size()) {
        return core::failure(core::ErrorCode::invariant,
                             "A binary output row does not match its encoded width");
    }
    std::ranges::copy(samples, bytes.begin());
    return {};
}
} // namespace docenhance::image
