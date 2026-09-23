// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <string_view>

namespace docenhance::contract {

// Validates scalar encoding, not assigned characters or normalization. NUL is valid Unicode;
// path admission rejects it separately. No allocation, replacement, locale or byte rewriting.
[[nodiscard]] constexpr bool valid_utf8(std::string_view text) noexcept {
    struct Utf8Lead {
        unsigned char first;
        unsigned char last;
        std::size_t remaining;
        unsigned char second_min;
        unsigned char second_max;
    };
    // Unicode Table 3-7. Only the first continuation needs the restricted subranges below.
    constexpr auto utf8_leads = std::to_array<Utf8Lead>({
        {.first = 0xc2, .last = 0xdf, .remaining = 1, .second_min = 0x80, .second_max = 0xbf},
        {.first = 0xe0, .last = 0xe0, .remaining = 2, .second_min = 0xa0, .second_max = 0xbf},
        {.first = 0xe1, .last = 0xec, .remaining = 2, .second_min = 0x80, .second_max = 0xbf},
        {.first = 0xed, .last = 0xed, .remaining = 2, .second_min = 0x80, .second_max = 0x9f},
        {.first = 0xee, .last = 0xef, .remaining = 2, .second_min = 0x80, .second_max = 0xbf},
        {.first = 0xf0, .last = 0xf0, .remaining = 3, .second_min = 0x90, .second_max = 0xbf},
        {.first = 0xf1, .last = 0xf3, .remaining = 3, .second_min = 0x80, .second_max = 0xbf},
        {.first = 0xf4, .last = 0xf4, .remaining = 3, .second_min = 0x80, .second_max = 0x8f},
    });
    const auto lead_for = [&utf8_leads](unsigned char byte) noexcept {
        for (const auto& lead : utf8_leads) {
            if (byte >= lead.first && byte <= lead.last) {
                return lead;
            }
        }
        return Utf8Lead{};
    };

    constexpr unsigned char ascii_max = 0x7f;
    constexpr unsigned char continuation_min = 0x80;
    constexpr unsigned char continuation_max = 0xbf;
    std::size_t remaining = 0;
    unsigned char low = continuation_min;
    unsigned char high = continuation_max;
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (remaining != 0) {
            if (byte < low || byte > high) {
                return false;
            }
            --remaining;
            low = continuation_min;
            high = continuation_max;
        } else if (byte > ascii_max) {
            const auto lead = lead_for(byte);
            remaining = lead.remaining;
            low = lead.second_min;
            high = lead.second_max;
            if (remaining == 0) {
                return false;
            }
        }
    }
    return remaining == 0;
}
} // namespace docenhance::contract
