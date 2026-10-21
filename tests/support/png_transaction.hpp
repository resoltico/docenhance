// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/codec/png_output.hpp"
#include "docenhance/fs/publication.hpp"
#include "docenhance/image/output.hpp"
#include "native_publication.hpp"
#include "png_depth_fixture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace docenhance::tests {
inline codec::PngImage binary_png(image::PlaneView<const std::uint8_t> pixels) {
    return {
        .shape =
            {
                .width = pixels.width(),
                .height = pixels.height(),
                .depth = docenhance::tests::fixture_png_depth(1),
            },
        .pixels = pixels,
        .profile = std::span<const std::byte>(),
        .resolution = std::nullopt,
    };
}
struct PngComparison {
    std::span<const std::byte> bytes;
    codec::PngImage intended;
};
inline fs::PublishResult<std::string>
publish_compared_png(const std::string& directory, PngComparison input, core::Budget& budget,
                     const core::Cancellation& cancellation = {},
                     fs::PublishRename commit = fs::rename_exclusive) {
    auto compared = codec::compare_png(input.bytes, input.intended, budget, cancellation);
    if (!compared) {
        return std::unexpected(fs::PublishFailure{.error = std::move(compared.error())});
    }
    const std::array files{fs::BundleFile{.relative = "result.png", .bytes = input.bytes}};
    return fs::publish_bundle(directory, files, cancellation, commit);
}
// Codec work and independent comparison finish before FS owns a staging directory.
// The encoded owner stays alive throughout synchronous comparison and byte publication.
inline fs::PublishResult<std::string>
publish_png_fixture(const std::string& directory, codec::PngImage image, core::Budget& budget,
                    const core::Cancellation& cancellation = {},
                    fs::PublishRename commit = fs::rename_exclusive) {
    auto encoded = codec::encode_png(image, budget, cancellation);
    if (!encoded) {
        return std::unexpected(fs::PublishFailure{.error = std::move(encoded.error())});
    }
    return publish_compared_png(directory, {.bytes = encoded->bytes(), .intended = image}, budget,
                                cancellation, commit);
}
} // namespace docenhance::tests
