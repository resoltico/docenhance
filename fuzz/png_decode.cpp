// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/codec/continuous_png.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"
#include "support/png_shape.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
namespace {
constexpr std::size_t memory_limit = std::size_t{8} * 1024 * 1024;

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
    docenhance::image::RasterShape shape;
    std::vector<std::uint8_t> samples;
    bool operator==(const Decoded&) const = default;
};
Decoded decode(std::span<const std::uint8_t> bytes, std::size_t limit) {
    using docenhance::fuzz::require;
    docenhance::core::Budget budget{limit};
    Decoded snapshot;
    {
        auto decoded = docenhance::codec::decode_png_raster(
            bytes, budget, docenhance::image::ProfilePolicy::embedded, {},
            {.encoded_bytes = 65536, .pixels = 4096});
        if (decoded) {
            require(static_container(bytes), "accepted source PNG is one static framed container");
            snapshot.accepted = true;
            snapshot.shape = decoded->shape;
            const auto row_bytes = docenhance::image::raster_row_bytes(snapshot.shape);
            require(row_bytes.has_value(),
                    "decoded typed channel/depth shape has bounded row bytes");
            require(snapshot.shape.width != 0 && snapshot.shape.height != 0,
                    "decoded dimensions are positive");
            require(static_cast<std::uint64_t>(snapshot.shape.width) * snapshot.shape.height <=
                        4096,
                    "decoded dimensions respect the pixel limit");
            for (std::uint32_t y = 0; y < snapshot.shape.height; ++y) {
                const auto row = decoded->pixels.view().row(y);
                require(row.size() == *row_bytes,
                        "snapshot includes every logical channel/depth byte");
                snapshot.samples.insert(snapshot.samples.end(), row.begin(), row.end());
            }
        } else {
            snapshot.error = decoded.error().code;
            if (docenhance::fuzz::zero_png_dimension(bytes) && limit == memory_limit) {
                require(snapshot.error == docenhance::core::ErrorCode::input,
                        "zero PNG dimensions are malformed input, not resources");
            }
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

    docenhance::fuzz::require(decode(bytes, memory_limit) == decode(bytes, memory_limit),
                              "repeated byte-span decoding is deterministic");
    const std::size_t constrained = bytes.empty() ? 0 : std::size_t{bytes.back()} * 1024;
    static_cast<void>(decode(bytes, constrained));
    return 0;
}
