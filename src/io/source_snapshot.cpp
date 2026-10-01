// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "source_snapshot.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/source.hpp"
#include "png_context.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace docenhance::io {
core::Result<core::Buffer> read_source_snapshot(const std::string& input, core::Budget& budget,
                                                const core::Cancellation& cancellation) {
    if (cancellation.requested(core::Checkpoint::decode)) {
        return core::cancelled();
    }
    const auto path = utf8_path(input);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return core::failure(core::ErrorCode::input,
                             "Source input must be a readable regular file");
    }
    // Reading bytes needs no codec: building a libpng context here would charge the budget for
    // structures this function never uses, and would report exhaustion as an unreadable input.
    const auto file = open_for_reading(path);
    if (file == nullptr || std::fseek(file.get(), 0, SEEK_END) != 0) {
        return core::failure(core::ErrorCode::input, "Cannot open or inspect Source input");
    }
    const auto length = std::ftell(file.get());
    if (length < 0 || std::fseek(file.get(), 0, SEEK_SET) != 0) {
        return core::failure(core::ErrorCode::input, "Cannot inspect Source input size");
    }
    if (std::cmp_greater(length, image::source_encoded_bytes_max)) {
        return core::failure(core::ErrorCode::resource, "Source exceeds its encoded input ceiling");
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
        if (std::fread(part.data(), 1, part.size(), file.get()) != part.size()) {
            return core::failure(core::ErrorCode::input,
                                 "Source changed size or could not be read");
        }
        remaining = remaining.subspan(part.size());
    }
    if (std::fgetc(file.get()) != EOF || std::ferror(file.get()) != 0) {
        return core::failure(core::ErrorCode::input,
                             "Source changed size or could not be finished");
    }
    return bytes;
}
} // namespace docenhance::io
