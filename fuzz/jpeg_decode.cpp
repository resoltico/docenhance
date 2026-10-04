// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/memory.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/jpeg.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using docenhance::fuzz::require;
    docenhance::core::Budget budget{std::size_t{8} * 1024 * 1024};
    {
        const auto decoded = docenhance::io::decode_jpeg(
            {data, size}, budget, docenhance::image::ProfilePolicy::embedded, {},
            {.encoded_bytes = 65536, .pixels = 65536, .scans = 16});
        if (decoded) {
            require(docenhance::image::valid_source_description(decoded->source),
                    "accepted JPEG has complete valid source observations");
            require(decoded->raster.shape.depth == docenhance::image::byte_bits,
                    "JPEG has no hidden precision mode");
            require(decoded->raster.metadata.png() == nullptr,
                    "JPEG does not pretend to carry PNG color declarations");
            require(decoded->raster.pixels.width() ==
                        decoded->raster.shape.width *
                            docenhance::image::components(decoded->raster.shape.model),
                    "decoded row has the declared component count");
        }
    }
    require(budget.used() == 0, "JPEG success and native failures refund every owned buffer");
    return 0;
}
