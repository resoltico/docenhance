// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/result.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "docenhance/methods/binary_facts.hpp"

#include <cstdint>
#include <functional>
#include <span>
namespace docenhance::methods {
struct BinaryMoments {
    std::uint64_t sum = 0;
    std::uint64_t squares = 0;
    void add(BinaryMoments other, std::uint64_t count = 1) noexcept {
        sum += other.sum * count;
        squares += other.squares * count;
    }
    void remove(BinaryMoments other) noexcept {
        sum -= other.sum;
        squares -= other.squares;
    }
};
struct BinaryColumns {
    std::span<std::uint64_t> sums;
    std::span<std::uint64_t> squares;
    std::uint32_t first{};
    [[nodiscard]] BinaryMoments at(std::uint32_t column) const noexcept {
        const auto index = column - first;
        return {
            .sum = sums.subspan(index, 1).front(),
            .squares = squares.subspan(index, 1).front(),
        };
    }
};
struct BinarySauvolaStrip {
    BinarySauvolaStrip(BinaryInput source, const Sauvola& selected, std::uint32_t begin,
                       std::uint32_t finish, BinaryColumns workspace) noexcept
        : input(source), method(selected), first(begin), end(finish), columns(workspace) {}
    BinaryInput input;
    std::reference_wrapper<const Sauvola> method;
    std::uint32_t first;
    std::uint32_t end;
    BinaryColumns columns;
};
[[nodiscard]] core::Result<ClassificationFacts>
binary_sauvola_strip(const BinarySauvolaStrip& context);
} // namespace docenhance::methods
