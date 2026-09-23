// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace docenhance::tests {
// Independent PNG fixture construction, without libpng or a production encoder. Inputs are
// already quantized sample indices; dimensions are bounded by the test/harness caller to 32.
struct GrayFixture {
    std::uint32_t width;
    std::uint32_t height;
    unsigned depth;
    bool interlaced;
    unsigned filter = 0;
    std::vector<std::uint8_t> samples;
};
inline void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (const unsigned shift : {24U, 16U, 8U, 0U}) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}
inline void append_le16(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}
inline std::uint32_t png_crc(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0 ? 0xedb88320U : 0U);
        }
    }
    return crc ^ 0xffffffffU;
}
inline void append_chunk(std::vector<std::uint8_t>& png, std::string_view name,
                         std::span<const std::uint8_t> bytes) {
    append_be32(png, static_cast<std::uint32_t>(bytes.size()));
    const auto start = png.size();
    for (const char byte : name) {
        png.push_back(static_cast<std::uint8_t>(byte));
    }
    png.insert(png.end(), bytes.begin(), bytes.end());
    const auto crc = png_crc(std::span{png}.subspan(start));
    append_be32(png, crc);
}
inline std::vector<std::uint8_t> stored_deflate(std::span<const std::uint8_t> raw) {
    std::vector<std::uint8_t> out{0x78, 0x01, 0x01}; // zlib header; final stored block
    const auto count = static_cast<std::uint32_t>(raw.size());
    append_le16(out, count);
    append_le16(out, count ^ 0xffffU);
    out.insert(out.end(), raw.begin(), raw.end());
    constexpr std::uint32_t adler_modulus = 65521;
    std::uint32_t first = 1;
    std::uint32_t second = 0;
    for (const auto byte : raw) {
        first = (first + byte) % adler_modulus;
        second = (second + first) % adler_modulus;
    }
    append_be32(out, (second << 16U) | first);
    return out;
}
struct Adam7Pass {
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t dx;
    std::uint32_t dy;
};
// PNG section 9: prediction uses unfiltered bytes, even for packed low-bit samples.
inline unsigned predictor(unsigned filter, unsigned left, unsigned above, unsigned corner) {
    if (filter == 1) {
        return left;
    }
    if (filter == 2) {
        return above;
    }
    if (filter == 3) {
        return (left + above) / 2;
    }
    if (filter == 4) {
        const int p = static_cast<int>(left + above) - static_cast<int>(corner);
        const int a = std::abs(p - static_cast<int>(left));
        const int b = std::abs(p - static_cast<int>(above));
        const int c = std::abs(p - static_cast<int>(corner));
        if (a <= b && a <= c) {
            return left;
        }
        return b <= c ? above : corner;
    }
    return 0;
}
inline void append_filtered(std::vector<std::uint8_t>& raw, const std::vector<std::uint8_t>& row,
                            const std::vector<std::uint8_t>& previous, unsigned filter) {
    raw.push_back(static_cast<std::uint8_t>(filter));
    for (std::size_t x = 0; x < row.size(); ++x) {
        const unsigned left = x == 0 ? 0 : row.at(x - 1);
        const unsigned above = previous.empty() ? 0 : previous.at(x);
        const unsigned corner = x == 0 || previous.empty() ? 0 : previous.at(x - 1);
        const auto prediction = predictor(filter, left, above, corner);
        raw.push_back(static_cast<std::uint8_t>((row.at(x) + 256U - prediction) & 0xffU));
    }
}
inline void append_pass(std::vector<std::uint8_t>& raw, const GrayFixture& fixture,
                        Adam7Pass pass) {
    if (pass.x >= fixture.width || pass.y >= fixture.height) {
        return;
    }
    std::vector<std::uint8_t> previous;
    for (auto y = pass.y; y < fixture.height; y += pass.dy) {
        std::vector<std::uint8_t> row;
        unsigned packed = 0;
        unsigned bits = 0;
        for (auto x = pass.x; x < fixture.width; x += pass.dx) {
            packed = (packed << fixture.depth) | fixture.samples.at((y * fixture.width) + x);
            bits += fixture.depth;
            if (bits == 8) {
                row.push_back(static_cast<std::uint8_t>(packed));
                packed = 0;
                bits = 0;
            }
        }
        if (bits != 0) {
            row.push_back(static_cast<std::uint8_t>(packed << (8U - bits)));
        }
        append_filtered(raw, row, previous, fixture.filter);
        previous = std::move(row);
    }
}
inline std::vector<std::uint8_t> make_gray_png(const GrayFixture& fixture) {
    std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<std::uint8_t> header;
    append_be32(header, fixture.width);
    append_be32(header, fixture.height);
    header.insert(header.end(), {
                                    static_cast<std::uint8_t>(fixture.depth),
                                    0,
                                    0,
                                    0,
                                    static_cast<std::uint8_t>(fixture.interlaced),
                                });
    append_chunk(png, "IHDR", header);
    std::vector<std::uint8_t> raw;
    if (fixture.interlaced) {
        constexpr auto passes = std::to_array<Adam7Pass>({
            {.x = 0, .y = 0, .dx = 8, .dy = 8},
            {.x = 4, .y = 0, .dx = 8, .dy = 8},
            {.x = 0, .y = 4, .dx = 4, .dy = 8},
            {.x = 2, .y = 0, .dx = 4, .dy = 4},
            {.x = 0, .y = 2, .dx = 2, .dy = 4},
            {.x = 1, .y = 0, .dx = 2, .dy = 2},
            {.x = 0, .y = 1, .dx = 1, .dy = 2},
        });
        for (const auto pass : passes) {
            append_pass(raw, fixture, pass);
        }
    } else {
        append_pass(raw, fixture, {.x = 0, .y = 0, .dx = 1, .dy = 1});
    }
    const auto compressed = stored_deflate(raw);
    append_chunk(png, "IDAT", compressed);
    append_chunk(png, "IEND", {});
    return png;
}
} // namespace docenhance::tests
