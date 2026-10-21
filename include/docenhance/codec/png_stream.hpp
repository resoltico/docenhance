// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"

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
struct PngOutput {
    void* state = nullptr;
    bool (*open)(void*) = nullptr;
    bool (*write)(void*, std::span<const std::uint8_t>) noexcept = nullptr;
    bool (*flush)(void*) noexcept = nullptr;
};
} // namespace docenhance::codec
