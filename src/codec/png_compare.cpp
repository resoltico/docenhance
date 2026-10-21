// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/png_artifact.hpp"
#include "docenhance/codec/png_output.hpp"
#include "docenhance/codec/png_stream.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/image/raster.hpp"
#include "png_context.hpp"
#include "png_rows.hpp"

#include <algorithm>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <png.h>
#include <span>
#include <utility>

namespace docenhance::codec {
namespace {
bool read_memory(void* const state, std::span<std::uint8_t> output) noexcept {
    auto& bytes = *static_cast<std::span<const std::byte>*>(state);
    if (output.size() > bytes.size()) {
        return false;
    }
    std::ranges::copy(bytes.first(output.size()), std::as_writable_bytes(output).begin());
    bytes = bytes.subspan(output.size());
    return true;
}
bool begin(PngContext& context, PngInput& input, image::PngShape shape) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // Context/input and all native or payload owners live in read_canonical_png.
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(std::begin(context.memory.jump)) != 0) {
        return false;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    install_png_reader(context, input);
    png_set_crc_action(context.png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
    png_set_chunk_malloc_max(context.png, image::profile_limit + 1);
    png_read_info(context.png, context.info);
    if (png_get_image_width(context.png, context.info) != shape.width ||
        png_get_image_height(context.png, context.info) != shape.height ||
        static_cast<unsigned>(png_get_bit_depth(context.png, context.info)) != shape.depth.bits() ||
        png_get_color_type(context.png, context.info) !=
            (shape.model == image::SampleModel::gray ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_RGB) ||
        png_get_interlace_type(context.png, context.info) != PNG_INTERLACE_NONE) {
        return false;
    }
    if (shape.depth.bits() == 1) {
        install_canonical_gray1_tail(context, shape.width);
    }
    // No packing, expansion, byte swap, gamma, filler or other sample transform is installed.
    png_read_update_info(context.png, context.info);
    return true;
}
bool read_row(PngContext& context, std::span<std::uint8_t> row) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // The row owner remains outside this trivial jump frame.
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(std::begin(context.memory.jump)) != 0) {
        return false;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    png_read_row(context.png, row.data(), nullptr);
    return true;
}
bool finish(PngContext& context) {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
    // No C++ owner is local to the native finish frame.
    // NOLINTNEXTLINE(cert-err52-cpp,modernize-avoid-setjmp-longjmp)
    if (setjmp(std::begin(context.memory.jump)) != 0) {
        return false;
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    png_read_end(context.png, context.info);
    return true;
}
core::Result<void> check_row(std::span<const std::uint8_t> row, PngArtifact& artifact,
                             const PngImage* const intended, std::uint32_t y,
                             const core::Cancellation& cancellation) {
    const bool binary = artifact.shape.depth.bits() == 1;
    const auto count = binary ? artifact.shape.width : row.size();
    const auto expected =
        intended == nullptr ? std::span<const std::uint8_t>{} : intended->pixels.row(y);
    for (std::size_t x = 0; x < count; ++x) {
        constexpr std::size_t interval = 4096;
        if (x % interval == 0 && cancellation.requested(core::Checkpoint::verification)) {
            return core::cancelled();
        }
        const auto value =
            binary ? ((static_cast<unsigned>(row.subspan(x / 8, 1).front()) >> (7 - (x % 8))) & 1U)
                   : row.subspan(x, 1).front();
        if (!expected.empty() && value != expected.subspan(x, 1).front()) {
            return core::failure(core::ErrorCode::input, "PNG pixels differ from intended output");
        }
        if (binary) {
            artifact.white_samples += static_cast<std::uint64_t>(value != 0);
        } else {
            artifact.mask_samples = artifact.mask_samples && value <= 1;
            artifact.protected_samples += static_cast<std::uint64_t>(value != 0);
        }
    }
    return {};
}
} // namespace
core::Result<PngArtifact> read_canonical_png(std::span<const std::byte> bytes,
                                             const PngImage* const intended, core::Budget& budget,
                                             const core::Cancellation& cancellation) {
    auto description = describe_canonical_png(bytes, intended, budget, cancellation);
    if (!description) {
        return std::unexpected(std::move(description.error()));
    }
    auto artifact = std::move(*description);
    const auto shape = artifact.shape;
    const bool mask = artifact.profile.empty() && !artifact.binary_samples;
    auto remaining = bytes;
    PngInput input{
        .state = &remaining,
        .read = read_memory,
        .remaining = bytes.size(),
        .checkpoint = core::Checkpoint::verification,
    };
    PngContext context{budget, false, cancellation};
    if (!context.ready() || !begin(context, input, shape)) {
        return std::unexpected(context.error(core::ErrorCode::input));
    }
    const auto row_bytes = artifact.binary_samples
                               ? (std::size_t{shape.width} + 7) / 8
                               : std::size_t{shape.width} * image::components(shape.model) *
                                     (shape.depth.bits() / image::byte_bits);
    if (png_get_rowbytes(context.png, context.info) != row_bytes) {
        return core::failure(core::ErrorCode::input, "Noncanonical PNG row layout");
    }
    auto row =
        image::Plane<std::uint8_t>::allocate(budget, static_cast<std::uint32_t>(row_bytes), 1);
    if (!row) {
        return std::unexpected(std::move(row.error()));
    }
    std::ranges::fill(row->view().row(0), 0);
    for (std::uint32_t y = 0; y < shape.height; ++y) {
        if (cancellation.requested(core::Checkpoint::verification)) {
            return core::cancelled();
        }
        if (!read_row(context, row->view().row(0))) {
            return std::unexpected(context.error(core::ErrorCode::input));
        }
        // Read the original pre-combine observation only after native success. Caller tail
        // bits may have been restored by libpng and are never treated as source evidence.
        if (context.canonical.invalid_layout || context.canonical.noncanonical_tail) {
            return core::failure(core::ErrorCode::input, "Noncanonical PNG packed tail or layout");
        }
        if (auto checked = check_row(row->view().row(0), artifact, intended, y, cancellation);
            !checked) {
            return std::unexpected(std::move(checked.error()));
        }
    }
    if (!finish(context)) {
        return std::unexpected(context.error(core::ErrorCode::input));
    }
    if (mask && !artifact.mask_samples) {
        return core::failure(core::ErrorCode::input, "Canonical mask samples must be zero or one");
    }
    return artifact;
}
core::Result<void> compare_png(std::span<const std::byte> bytes, PngImage intended,
                               core::Budget& budget, const core::Cancellation& cancellation) {
    if (!image::validate(intended.shape) || intended.pixels.empty()) {
        return core::failure(core::ErrorCode::argument, "Invalid intended PNG output");
    }
    auto observed = read_canonical_png(bytes, &intended, budget, cancellation);
    if (!observed) {
        auto error = std::move(observed.error());
        if (error.code == core::ErrorCode::input) {
            error.code = core::ErrorCode::output_verify;
        }
        return std::unexpected(std::move(error));
    }
    return {};
}
} // namespace docenhance::codec
