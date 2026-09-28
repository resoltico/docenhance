// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/png.hpp"
#include "png_context.hpp"
#include "png_rows.hpp"
#include "publication.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace docenhance::io {
core::Result<void> write_verified_png(const BundleSlot& slot,
                                      image::PlaneView<const std::uint8_t> image,
                                      core::Budget& budget,
                                      const core::Cancellation& cancellation) {
    auto encoded = encode_png(slot.path, image, budget, cancellation);
    if (!encoded) {
        return encoded;
    }
    // Earn the claim the same way continuous output does: reopen what was written and compare it
    // against the intended samples and metadata.
    return verify_png_image(slot.path, image, budget, cancellation);
}
core::Result<std::string> publish_png(const std::string& output_directory,
                                      image::PlaneView<const std::uint8_t> image,
                                      core::Budget& budget, const core::Cancellation& cancellation,
                                      PublishRename commit) {
    struct BinaryWriter {
        image::PlaneView<const std::uint8_t> image;
        std::reference_wrapper<core::Budget> budget;
        std::reference_wrapper<const core::Cancellation> cancellation;
        BinaryWriter(image::PlaneView<const std::uint8_t> view, core::Budget& owner,
                     const core::Cancellation& control)
            : image(view), budget(owner), cancellation(control) {}
    };
    if (image.empty()) {
        return core::failure(core::ErrorCode::argument, "Cannot publish an empty image");
    }
    BinaryWriter state{image, budget, cancellation};
    const PngWriterRef writer{
        .state = &state,
        .write = [](void* raw, const BundleSlot& slot) -> core::Result<void> {
            auto const& value = *static_cast<BinaryWriter*>(raw);
            return write_verified_png(slot, value.image, value.budget.get(),
                                      value.cancellation.get());
        },
    };
    return publish_generated_png(output_directory, writer, cancellation, commit);
}
core::Result<std::string> publish_grayscale_png(const std::string& output_directory,
                                                image::PlaneView<const std::uint8_t> image,
                                                core::Budget& budget,
                                                const core::Cancellation& cancellation) {
    return publish_png(output_directory, image, budget, cancellation, rename_exclusive);
}
} // namespace docenhance::io
