// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "png_context.hpp"
#include "png_rows.hpp"

#include <cstddef>
#include <cstdint>
#include <png.h>
#include <pngconf.h>
#include <span>

namespace docenhance::codec {
namespace {
void PNGCBAPI inspect_packed_tail(png_structp png, png_row_infop info, png_bytep bytes) noexcept {
    auto* const state = static_cast<CanonicalRowState*>(png_get_user_transform_ptr(png));
    if (state == nullptr) {
        return;
    }
    const auto width = state->expected_width;
    const auto row_bytes = (width / 8) + static_cast<unsigned>(width % 8 != 0);
    if (info == nullptr || bytes == nullptr || width == 0 || info->width != width ||
        info->bit_depth != 1 || info->channels != 1 || info->pixel_depth != 1 ||
        info->color_type != PNG_COLOR_TYPE_GRAY || info->rowbytes != row_bytes) {
        state->invalid_layout = true;
        return;
    }
    const auto remainder = width % 8;
    if (remainder != 0) {
        const auto mask = (1U << (8 - remainder)) - 1;
        const auto tail = std::span{bytes, std::size_t{row_bytes}}.last(1).front();
        state->noncanonical_tail = state->noncanonical_tail || (tail & mask) != 0;
    }
}
} // namespace
void install_canonical_gray1_tail(PngContext& context, std::uint32_t width) {
    context.canonical = {.expected_width = width};
    // Install after header admission, before update/start/read. Zero overrides preserve PNG1.
    png_set_user_transform_info(context.png, &context.canonical, 0, 0);
    png_set_read_user_transform_fn(context.png, inspect_packed_tail);
}
} // namespace docenhance::codec
