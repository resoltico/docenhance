// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/image/output.hpp"

#include <stdexcept>
namespace docenhance::tests {
inline image::PngDepth fixture_png_depth(unsigned bits) {
    if (const auto depth = image::PngDepth::from_bits(bits)) {
        return *depth;
    }
    throw std::logic_error("PNG fixture depth is outside the admitted domain");
}
} // namespace docenhance::tests
