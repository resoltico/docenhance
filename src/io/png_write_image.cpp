// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "png_context.hpp"
#include "png_rows.hpp"
#include "publication.hpp"

#include <cstdint>

namespace docenhance::io {
core::Result<void> write_verified_png(const BundleSlot& slot,
                                      image::PlaneView<const std::uint8_t> image,
                                      core::Budget& budget,
                                      const core::Cancellation& cancellation) {
    if (!slot.valid_parent() || slot.created == nullptr) {
        return core::failure(core::ErrorCode::output, "The reserved PNG parent changed");
    }
    auto encoded = encode_png(slot, image, budget, cancellation);
    if (!encoded) {
        return encoded;
    }
    // Earn the claim the same way continuous output does: reopen what was written and compare it
    // against the intended samples and metadata.
    return verify_png_image(slot.path, image, budget, cancellation, slot.created);
}
} // namespace docenhance::io
