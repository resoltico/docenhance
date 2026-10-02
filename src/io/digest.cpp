// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/io/digest.hpp"

#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "png_context.hpp"
#include "publication.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <picosha2.h>
#include <span>
#include <string>
#include <utility>

namespace docenhance::io {
core::Result<core::ContentIdentity> identify_slot(const BundleSlot& slot, std::uint64_t limit,
                                                  const core::Cancellation& cancellation) {
    const auto file = open_for_reading(slot.path);
    if (file == nullptr) {
        return core::failure(core::ErrorCode::output, "Cannot open a bundle file to identify it");
    }
    constexpr std::size_t transfer = std::size_t{64} * 1024;
    std::array<std::uint8_t, transfer> buffer{};
    picosha2::hash256_one_by_one hasher;
    std::uint64_t total = 0;
    while (true) {
        if (cancellation.requested(core::Checkpoint::verification)) {
            return core::cancelled();
        }
        const auto read = std::fread(buffer.data(), 1, buffer.size(), file.get());
        if (std::ferror(file.get()) != 0) {
            return core::failure(core::ErrorCode::output, "A bundle file could not be read back");
        }
        if (read == 0) {
            break;
        }
        total += read;
        // Stopping here reads no further: the bound is on what this reads, not on what a
        // measurement taken beforehand promised.
        if (total > limit) {
            return core::failure(core::ErrorCode::input, "A bundle file is larger than its bound");
        }
        const auto part = std::span{buffer}.first(read);
        hasher.process(part.begin(), part.end());
    }
    hasher.finish();
    try {
        std::string hex;
        picosha2::get_hash_hex_string(hasher, hex);
        if (hex.size() != core::sha256_hex_length) {
            return core::failure(core::ErrorCode::invariant, "A digest was not fully rendered");
        }
        return core::ContentIdentity{.sha256 = std::move(hex), .bytes = total};
    } catch (const std::bad_alloc&) {
        return core::failure(core::ErrorCode::resource, "Identifying content exhausted memory");
    }
}
core::Result<core::ContentIdentity> identify(std::span<const std::byte> content,
                                             const core::Cancellation& cancellation) {
    // uint8_t is the unsigned-byte view of the same immutable storage.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const std::span bytes{reinterpret_cast<const std::uint8_t*>(content.data()), content.size()};
    try {
        std::string hex;
        picosha2::hash256_one_by_one hasher;
        auto remaining = bytes;
        constexpr std::size_t transfer = std::size_t{64} * 1024;
        while (!remaining.empty()) {
            if (cancellation.requested(core::Checkpoint::verification)) {
                return core::cancelled();
            }
            const auto part = remaining.first(std::min(transfer, remaining.size()));
            hasher.process(part.begin(), part.end());
            remaining = remaining.subspan(part.size());
        }
        hasher.finish();
        picosha2::get_hash_hex_string(hasher, hex);
        if (hex.size() != core::sha256_hex_length) {
            return core::failure(core::ErrorCode::invariant, "A digest was not fully rendered");
        }
        return core::ContentIdentity{.sha256 = std::move(hex), .bytes = content.size()};
    } catch (const std::bad_alloc&) {
        return core::failure(core::ErrorCode::resource, "Identifying content exhausted memory");
    }
}
} // namespace docenhance::io
