// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
namespace {
// Independent admission oracle: walk framing only, without importing the production scanner.
bool static_container(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t signature = 8;
    constexpr std::size_t overhead = 12;
    constexpr std::size_t integer = 4;
    if (bytes.size() < signature) {
        return false;
    }
    bytes = bytes.subspan(signature);
    while (bytes.size() >= overhead) {
        std::size_t length = 0;
        for (const auto value : bytes.first(integer)) {
            length = (length * 256) + value;
        }
        if (length > bytes.size() - overhead) {
            return false;
        }
        const auto name = bytes.subspan(integer, integer);
        const auto is = [&](std::string_view wanted) { return std::ranges::equal(name, wanted); };
        if (is("acTL") || is("fcTL") || is("fdAT")) {
            return false;
        }
        bytes = bytes.subspan(length + overhead);
        if (is("IEND")) {
            return length == 0 && bytes.empty();
        }
    }
    return false;
}
struct Decoded {
    bool accepted = false;
    docenhance::core::ErrorCode error = docenhance::core::ErrorCode::input;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> samples;
    bool operator==(const Decoded&) const = default;
};
Decoded decode(std::span<const std::uint8_t> bytes, std::size_t limit) {
    using docenhance::fuzz::require;
    docenhance::core::Budget budget{limit};
    Decoded snapshot;
    {
        auto decoded = docenhance::io::decode_grayscale_png(
            bytes, budget, {.encoded_bytes = 65536, .pixels = 4096});
        if (decoded) {
            require(static_container(bytes),
                    "accepted grayscale PNG is one static framed container");
            snapshot.accepted = true;
            snapshot.width = decoded->width();
            snapshot.height = decoded->height();
            require(snapshot.width != 0 && snapshot.height != 0, "decoded dimensions are positive");
            require(static_cast<std::uint64_t>(snapshot.width) * snapshot.height <= 4096,
                    "decoded dimensions respect the pixel limit");
            for (std::uint32_t y = 0; y < snapshot.height; ++y) {
                const auto row = decoded->view().row(y);
                snapshot.samples.insert(snapshot.samples.end(), row.begin(), row.end());
            }
        } else {
            snapshot.error = decoded.error().code;
            require(snapshot.error == docenhance::core::ErrorCode::input ||
                        snapshot.error == docenhance::core::ErrorCode::resource,
                    "malformed input cannot become an invariant failure");
        }
        require(budget.used() <= limit,
                "codec and image allocations stay within their shared budget");
    }
    require(budget.used() == 0, "all decoder allocations are refunded after each invocation");
    return snapshot;
}
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> bytes{data, size};
    constexpr std::size_t memory_limit = std::size_t{8} * 1024 * 1024;
    docenhance::fuzz::require(decode(bytes, memory_limit) == decode(bytes, memory_limit),
                              "repeated byte-span decoding is deterministic");
    const std::size_t constrained = bytes.empty() ? 0 : std::size_t{bytes.back()} * 1024;
    static_cast<void>(decode(bytes, constrained));
    return 0;
}
