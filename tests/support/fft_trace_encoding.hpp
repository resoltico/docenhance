// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <ostream>
#include <span>
#include <string_view>
#include <vector>
#include <zlib.h>
namespace docenhance::tests::fft_allocation_trace::encoding {
constexpr std::size_t raw_limit = std::size_t{1024} * 1024;
inline bool base64(std::ostream& stream, std::span<const Bytef> bytes) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t offset = 0; offset < bytes.size(); offset += 3) {
        const auto chunk = bytes.subspan(offset, std::min(std::size_t{3}, bytes.size() - offset));
        const auto first = static_cast<unsigned>(chunk.front());
        const auto second = chunk.size() > 1 ? static_cast<unsigned>(chunk[1]) : 0U;
        const auto third = chunk.size() > 2 ? static_cast<unsigned>(chunk[2]) : 0U;
        const std::array block{
            alphabet[first >> 2U],
            alphabet[((first & 3U) << 4U) | (second >> 4U)],
            chunk.size() > 1 ? alphabet[((second & 15U) << 2U) | (third >> 6U)] : '=',
            chunk.size() > 2 ? alphabet[third & 63U] : '=',
        };
        stream.write(block.data(), static_cast<std::streamsize>(block.size()));
    }
    return stream.good();
}
// Only called after observing is false and every observed allocation has been
// refunded.
inline bool write(std::ostream& stream, std::string_view raw) {
    if (raw.size() > raw_limit) {
        return false;
    }
    std::vector<Bytef> input(0);
    input.reserve(raw.size());
    std::ranges::transform(raw, std::back_inserter(input),
                           [](char value) { return static_cast<Bytef>(value); });
    // An empty sequence still carries one complete zlib member, never a legacy
    // empty string.
    const Bytef empty = 0;
    const auto source_size = static_cast<uLong>(input.size());
    auto packed_size = compressBound(source_size);
    std::vector<Bytef> packed(packed_size);
    if (compress2(packed.data(), &packed_size, input.empty() ? &empty : input.data(), source_size,
                  Z_BEST_COMPRESSION) != Z_OK) {
        return false;
    }
    stream << "\"z1:";
    if (!base64(stream, std::span{packed}.first(packed_size))) {
        return false;
    }
    stream << '\"';
    return stream.good();
}
// The storage golden decodes emitted bytes independently of the forward encoder.
inline bool decode_base64(std::string_view text, std::vector<Bytef>& packed) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (text.empty() || text.size() % 4 != 0 || text.size() > 4 * ((raw_limit + 1026) / 3)) {
        return false;
    }
    packed.reserve((text.size() / 4) * 3);
    for (std::size_t offset = 0; offset < text.size(); offset += 4) {
        const auto block = text.substr(offset, 4);
        const auto first = alphabet.find(block[0]);
        const auto second = alphabet.find(block[1]);
        const auto third = block[2] == '=' ? 0 : alphabet.find(block[2]);
        const auto fourth = block[3] == '=' ? 0 : alphabet.find(block[3]);
        const bool final = offset + 4 == text.size();
        if (first >= 64 || second >= 64 || third >= 64 || fourth >= 64 ||
            (!final && (block[2] == '=' || block[3] == '=')) ||
            (block[2] == '=' && (block[3] != '=' || (second & 15U) != 0)) ||
            (block[3] == '=' && (third & 3U) != 0)) {
            return false;
        }
        packed.push_back(static_cast<Bytef>((first << 2U) | (second >> 4U)));
        if (block[2] != '=') {
            packed.push_back(static_cast<Bytef>((second << 4U) | (third >> 2U)));
        }
        if (block[3] != '=') {
            packed.push_back(static_cast<Bytef>((third << 6U) | fourth));
        }
    }
    return true;
}
inline bool matches(std::string_view emitted, std::string_view expected) {
    if (expected.size() > raw_limit || !emitted.starts_with("\"z1:") || !emitted.ends_with('\"')) {
        return false;
    }
    std::vector<Bytef> packed(0);
    if (!decode_base64(emitted.substr(4, emitted.size() - 5), packed)) {
        return false;
    }
    std::vector<Bytef> decoded(expected.size() + 1);
    auto decoded_size = static_cast<uLongf>(decoded.size());
    auto packed_size = static_cast<uLong>(packed.size());
    return uncompress2(decoded.data(), &decoded_size, packed.data(), &packed_size) == Z_OK &&
           packed_size == packed.size() && decoded_size == expected.size() &&
           std::equal(expected.begin(), expected.end(), decoded.begin(),
                      [](char left, Bytef right) { return static_cast<Bytef>(left) == right; });
}
} // namespace docenhance::tests::fft_allocation_trace::encoding
