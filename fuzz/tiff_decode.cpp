// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/memory.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/io/tiff.hpp"
#include "support/entry_point.hpp"
#include "support/oracle.hpp"

#include <cstddef>
#include <cstdint>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using docenhance::fuzz::require;
    docenhance::core::Budget budget{std::size_t{64} * 1024 * 1024};
    {
        const auto decoded = docenhance::io::decode_tiff(
            {data, size}, budget, docenhance::image::ProfilePolicy::embedded, {},
            {.encoded_bytes = 65536, .pixels = 65536});
        if (decoded) {
            require(docenhance::image::valid_tiff_source(decoded->source),
                    "accepted TIFF has complete source declarations");
            require(decoded->raster.shape == docenhance::image::decoded_tiff_shape(decoded->source),
                    "decoded TIFF precision and sample model agree with declarations");
            require(decoded->raster.metadata.png() == nullptr,
                    "TIFF color declarations remain distinct from PNG");
        }
    }
    require(budget.used() == 0, "TIFF success and failures refund all charged storage");
    return 0;
}
