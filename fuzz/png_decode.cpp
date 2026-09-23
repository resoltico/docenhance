// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace {
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
