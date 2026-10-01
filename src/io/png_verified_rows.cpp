// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "png_rows.hpp"
#include "publication.hpp"

namespace docenhance::io {
core::Result<void> write_verified_png_rows(const BundleSlot& slot, image::RowSource& rows,
                                           core::Budget& budget,
                                           const core::Cancellation& cancellation) {
    auto encoded = encode_png_rows(slot.path, rows, budget, cancellation, slot.created);
    if (!encoded) {
        return encoded;
    }
    return verify_png_rows(slot.path, rows, budget, cancellation);
}
} // namespace docenhance::io
