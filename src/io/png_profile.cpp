// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/raster.hpp"
#include "png_metadata.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <span>
#include <utility>
#include <zconf.h>
#include <zlib.h>

namespace docenhance::io {
namespace {
constexpr std::size_t inflate_blocks = 8;
struct InflateMemory {
    std::reference_wrapper<core::Budget> budget;
    std::array<core::Buffer, inflate_blocks> blocks;
    bool exhausted = false;
};
void* allocate_block(InflateMemory& state, std::uint64_t bytes) {
    if (bytes > state.budget.get().available()) {
        return nullptr;
    }
    for (auto& block : state.blocks) {
        if (!block.empty()) {
            continue;
        }
        auto result = state.budget.get().allocate(static_cast<std::size_t>(bytes));
        if (!result) {
            return nullptr;
        }
        block = std::move(*result);
        return block.bytes().data();
    }
    return nullptr;
}
voidpf allocate_inflate(voidpf opaque, uInt count, uInt size) noexcept {
    auto& state = *static_cast<InflateMemory*>(opaque);
    try {
        auto* const result = allocate_block(state, std::uint64_t{count} * size);
        if (result != nullptr) {
            return result;
        }
    } catch (...) {
        state.exhausted = true; // No exception may escape the native callback.
    }
    state.exhausted = true;
    return nullptr;
}
void free_inflate(voidpf opaque, voidpf pointer) noexcept {
    for (auto& block : static_cast<InflateMemory*>(opaque)->blocks) {
        if (block.bytes().data() == pointer) {
            block = core::Buffer{};
            return;
        }
    }
}
bool valid_keyword(std::span<const std::uint8_t> name) noexcept {
    constexpr std::size_t max_name = 79;
    constexpr std::uint8_t space = 32;
    constexpr std::uint8_t ascii_last = 126;
    constexpr std::uint8_t latin_first = 161;
    if (name.empty() || name.size() > max_name || name.front() == space || name.back() == space) {
        return false;
    }
    bool previous_space = false;
    for (const auto byte : name) {
        if (byte < space || (byte > ascii_last && byte < latin_first) ||
            (byte == space && previous_space)) {
            return false;
        }
        previous_space = byte == space;
    }
    return true;
}
struct InflateStream {
    InflateStream() = default;
    InflateStream(const InflateStream&) = delete;
    InflateStream& operator=(const InflateStream&) = delete;
    InflateStream(InflateStream&&) = delete;
    InflateStream& operator=(InflateStream&&) = delete;
    z_stream stream{};
    bool initialized = false;
    ~InflateStream() {
        if (initialized) {
            static_cast<void>(inflateEnd(&stream));
        }
    }
};
} // namespace
core::Result<core::Buffer> inflate_profile(std::span<const std::uint8_t> bytes,
                                           core::Budget& budget) {
    const auto separator = std::ranges::find(bytes, std::uint8_t{0});
    const auto name_size = static_cast<std::size_t>(separator - bytes.begin());
    if (separator == bytes.end() || !valid_keyword(bytes.first(name_size)) ||
        bytes.size() - name_size < 2 || bytes.subspan(name_size + 1, 1).front() != 0) {
        return core::failure(core::ErrorCode::input, "Malformed PNG ICC declaration");
    }
    bytes = bytes.subspan(name_size + 2);
    if (bytes.empty() || bytes.size() > image::profile_limit) {
        return core::failure(core::ErrorCode::input, "PNG ICC compressed data exceeds its limit");
    }
    auto storage = budget.allocate(image::profile_limit + 1);
    if (!storage) {
        return std::unexpected(storage.error());
    }
    InflateMemory memory{.budget = budget, .blocks = {}};
    InflateStream input;
    input.stream.zalloc = allocate_inflate;
    input.stream.zfree = free_inflate;
    input.stream.opaque = &memory;
    input.initialized = inflateInit(&input.stream) == Z_OK;
    if (!input.initialized) {
        return core::failure(core::ErrorCode::resource, "Cannot initialize ICC decompression");
    }
    // The pinned zlib API retains a mutable pointer but does not modify compressed input.
    input.stream.next_in =
        const_cast<Bytef*>(bytes.data()); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    input.stream.avail_in = static_cast<uInt>(bytes.size());
    // zlib writes serialized bytes into the budget-owned storage.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    input.stream.next_out = reinterpret_cast<Bytef*>(storage->bytes().data());
    input.stream.avail_out = static_cast<uInt>(storage->size());
    const int result = inflate(&input.stream, Z_FINISH);
    constexpr std::size_t icc_header = 128;
    if (input.stream.total_out > image::profile_limit) {
        return core::failure(core::ErrorCode::resource,
                             "PNG ICC data exceeds its uncompressed byte ceiling");
    }
    if (result != Z_STREAM_END || input.stream.avail_in != 0 ||
        input.stream.total_out < icc_header) {
        return core::failure(memory.exhausted ? core::ErrorCode::resource : core::ErrorCode::input,
                             "PNG ICC compressed data is malformed");
    }
    auto profile = budget.allocate(input.stream.total_out);
    if (!profile) {
        return std::unexpected(profile.error());
    }
    std::memcpy(profile->bytes().data(), storage->bytes().data(), profile->size());
    return profile;
}
} // namespace docenhance::io
