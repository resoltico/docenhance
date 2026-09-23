// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "png_context.hpp"
#include "png_reader.hpp"

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
bool read_file(void* const state, std::span<std::uint8_t> output) noexcept {
    return std::fread(output.data(), 1, output.size(), static_cast<std::FILE*>(state)) ==
           output.size();
}
} // namespace
core::Result<image::Plane<std::uint8_t>> load_grayscale_png(const std::string& input,
                                                            core::Budget& budget) {
    const auto path = utf8_path(input);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return core::failure(core::ErrorCode::input,
                             "The PNG input must be a readable regular file");
    }
    PngContext context{budget, false};
    if (!context.open(path)) {
        return std::unexpected(context.error(core::ErrorCode::input));
    }
    if (std::fseek(context.file.get(), 0, SEEK_END) != 0) {
        return core::failure(core::ErrorCode::input, "Cannot inspect the PNG input");
    }
    const auto size = std::ftell(context.file.get());
    if (size < 0 || std::fseek(context.file.get(), 0, SEEK_SET) != 0) {
        return core::failure(core::ErrorCode::input, "Cannot inspect the PNG input");
    }
    const PngLimits limits;
    if (std::cmp_greater(size, limits.encoded_bytes)) {
        return core::failure(core::ErrorCode::resource,
                             "The PNG input exceeds the 128 MiB file limit");
    }
    PngInput reader{
        .state = context.file.get(),
        .read = read_file,
        .remaining = static_cast<std::size_t>(size),
    };
    return decode_png(context, reader, limits);
}
} // namespace docenhance::io
