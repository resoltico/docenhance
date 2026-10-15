// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/png.hpp"
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/cancellation.hpp"
#include "png_context.hpp"
#include "png_metadata.hpp"
#include "png_rows.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace docenhance::codec {
bool output_inventory(PngContext& context, const PngReadStream& stream,
                      const core::Cancellation& cancellation) {
    // Read the inventory separately from the pixel decoder. Only writer-owned metadata is valid.
    // The following native decode checks CRCs and all payloads, including the final IEND.
    std::array<std::uint8_t, png_signature_bytes> header{};
    if (!stream.input.read(stream.input.state, header)) {
        return false;
    }
    constexpr std::uint32_t iccp = 0x69434350;
    constexpr std::uint32_t phys = 0x70485973;
    constexpr unsigned chunk_limit = 1000000;
    for (unsigned chunks = 0; chunks < chunk_limit; ++chunks) {
        if (cancellation.requested(core::Checkpoint::verification)) {
            context.memory.cancelled = true;
            return false;
        }
        if (!stream.input.read(stream.input.state, header)) {
            return false;
        }
        const auto size = png_integer(header);
        const auto type = png_integer(std::span{header}.subspan(png_integer_bytes));
        const bool allowed = type == chunk_ihdr || type == iccp || type == phys ||
                             type == chunk_idat || type == chunk_iend;
        if (!allowed || size > png_max_encoded_bytes) {
            return false;
        }
        if (!stream.skip(stream.input.state, size + png_integer_bytes)) {
            return false;
        }
        if (type == chunk_iend) {
            return size == 0 && stream.finish_inventory(stream.input.state);
        }
    }
    return false;
}
} // namespace docenhance::codec
