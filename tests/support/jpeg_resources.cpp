// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/io/jpeg.hpp"
#include "file_contents.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
namespace {
int observe(int count, const char* const* const values) {
    if (count != 2) {
        return 2;
    }
    const auto arguments = std::span{values, static_cast<std::size_t>(count)};
    const auto encoded = docenhance::tests::file_contents(std::filesystem::path{arguments[1]});
    docenhance::core::Budget budget{std::size_t{1024} * 1024 * 1024};
    auto snapshot = budget.allocate(encoded.size());
    if (!snapshot) {
        return 4;
    }
    const std::span text{encoded.data(), encoded.size()};
    std::ranges::copy(std::as_bytes(text), snapshot->bytes().begin());
    // Encoded byte view; the charged snapshot outlives native decoding and its statistics.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(snapshot->bytes().data());
    const auto result = docenhance::io::decode_jpeg({bytes, snapshot->size()}, budget,
                                                    docenhance::image::ProfilePolicy::embedded);
    if (!result) {
        std::cerr << result.error().message << '\n';
        return 4;
    }
    std::cout << "{\"native_block_peak\":" << result->native_block_peak
              << ",\"native_allocations\":" << result->native_allocations
              << ",\"decoder_charge_peak\":" << result->decoder_charge_peak
              << ",\"working_charge_peak\":" << result->working_charge_peak
              << ",\"retained_charge\":" << budget.used() << "}\n";
    return 0;
}

} // namespace
int main(int count,
         char** const values) { // NOLINT(misc-const-correctness): standard OS entry signature.
    try {
        return observe(count, values);
    } catch (...) {
        return 4;
    }
}
