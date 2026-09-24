// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "docenhance/io/png.hpp"
#include "png_context.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace docenhance::io {
namespace {
core::Result<core::Buffer> snapshot(const std::string& input, core::Budget& budget,
                                    const core::Cancellation& cancellation) {
    if (cancellation.requested(core::Checkpoint::decode)) {
        return core::cancelled();
    }
    const auto path = utf8_path(input);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return core::failure(core::ErrorCode::input, "PNG input must be a readable regular file");
    }
    PngContext file{budget, false, cancellation};
    if (!file.open(path) || std::fseek(file.file.get(), 0, SEEK_END) != 0) {
        return core::failure(core::ErrorCode::input, "Cannot open or inspect PNG input");
    }
    const auto length = std::ftell(file.file.get());
    if (length < 0 || std::fseek(file.file.get(), 0, SEEK_SET) != 0) {
        return core::failure(core::ErrorCode::input, "Cannot inspect PNG input size");
    }
    if (std::cmp_greater(length, png_max_encoded_bytes)) {
        return core::failure(core::ErrorCode::resource, "PNG exceeds its encoded input ceiling");
    }
    auto bytes = budget.allocate(static_cast<std::size_t>(length));
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    auto remaining = bytes->bytes();
    constexpr std::size_t transfer = std::size_t{64} * 1024;
    while (!remaining.empty()) {
        if (cancellation.requested(core::Checkpoint::decode)) {
            return core::cancelled();
        }
        auto const part = remaining.first(std::min(transfer, remaining.size()));
        if (std::fread(part.data(), 1, part.size(), file.file.get()) != part.size()) {
            return core::failure(core::ErrorCode::input, "PNG changed size or could not be read");
        }
        remaining = remaining.subspan(part.size());
    }
    if (std::fgetc(file.file.get()) != EOF || std::ferror(file.file.get()) != 0) {
        return core::failure(core::ErrorCode::input, "PNG changed size or could not be finished");
    }
    return bytes;
}
} // namespace
core::Result<image::Raster> load_png_raster(const std::string& input, core::Budget& budget,
                                            image::ProfilePolicy policy,
                                            const core::Cancellation& cancellation) {
    auto encoded = snapshot(input, budget, cancellation);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    // uint8_t is the unsigned-byte view of the immutable encoded snapshot.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* data = reinterpret_cast<const std::uint8_t*>(encoded->bytes().data());
    return decode_png_raster({data, encoded->size()}, budget, policy, cancellation);
}
} // namespace docenhance::io
