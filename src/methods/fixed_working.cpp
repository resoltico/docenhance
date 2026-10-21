// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "binary_detail.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/binarization_working.hpp"
#include "docenhance/methods/binary_facts.hpp"

#include <cstdint>
#include <expected>
namespace docenhance::methods {
core::Result<FixedFacts> binarize(const FixedThreshold& method, BinaryInput input) {
    if (auto admitted = validate_binary_input(input); !admitted) {
        return std::unexpected(admitted.error());
    }
    auto counts = binary_counts(input, BinaryPartition::rows);
    if (!counts) {
        return std::unexpected(counts.error());
    }
    const auto classify = [&](std::uint16_t value) noexcept {
        return static_cast<double>(value) / static_cast<double>(binary_code_max) <=
               method.threshold();
    };
    auto classified = binary_rows(input, counts->view(), classify);
    if (!classified) {
        return std::unexpected(classified.error());
    }
    FixedFacts facts;
    static_cast<ClassificationFacts&>(facts) = *classified;
    return facts;
}
} // namespace docenhance::methods
