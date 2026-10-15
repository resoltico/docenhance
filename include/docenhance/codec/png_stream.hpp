// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace docenhance::codec {
// State and every resource owner outlive the call. Native callbacks cannot throw.
struct PngInput {
    void* state = nullptr;
    bool (*read)(void*, std::span<std::uint8_t>) noexcept = nullptr;
    std::size_t remaining = 0;
    core::Checkpoint checkpoint = core::Checkpoint::decode;
};
struct PngReadStream {
    PngInput input;
    core::Result<void> (*open)(void*) = nullptr;
    bool (*skip)(void*, std::uint32_t) noexcept = nullptr;
    bool (*finish_inventory)(void*) noexcept = nullptr;
};
struct PngOutput {
    void* state = nullptr;
    bool (*open)(void*) = nullptr;
    bool (*write)(void*, std::span<const std::uint8_t>) noexcept = nullptr;
    bool (*flush)(void*) noexcept = nullptr;
};
[[nodiscard]] core::Result<void> encode_png(PngOutput output,
                                            image::PlaneView<const std::uint8_t> view,
                                            core::Budget& budget,
                                            const core::Cancellation& cancellation = {});
[[nodiscard]] core::Result<void> encode_png_rows(PngOutput output, image::RowSource& source,
                                                 core::Budget& budget,
                                                 const core::Cancellation& cancellation);
[[nodiscard]] core::Result<void> verify_png_rows(PngReadStream stream, image::RowSource& source,
                                                 core::Budget& budget,
                                                 const core::Cancellation& cancellation);
} // namespace docenhance::codec
