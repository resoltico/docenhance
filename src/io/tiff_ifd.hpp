// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
namespace docenhance::io {
inline constexpr std::uint64_t tiff_entries_max = 512;
struct TiffField {
    unsigned type{};
    std::uint64_t count{};
    std::span<const std::uint8_t> bytes;
};
struct TiffIfd {
    std::span<const std::uint8_t> bytes;
    std::size_t start{};
    unsigned entries{};
    bool little = false;
    bool big = false;
    [[nodiscard]] unsigned entry_size() const noexcept;
    [[nodiscard]] std::optional<TiffField> field(unsigned tag) const noexcept;
    [[nodiscard]] std::uint64_t number(std::span<const std::uint8_t> value) const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> integer(unsigned tag,
                                                       std::uint64_t fallback) const noexcept;
};
[[nodiscard]] core::Result<TiffIfd> scan_tiff(std::span<const std::uint8_t> bytes,
                                              const core::Cancellation& cancellation);
} // namespace docenhance::io
