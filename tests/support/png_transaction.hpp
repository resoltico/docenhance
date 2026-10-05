// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "docenhance/io/png.hpp"
#include "docenhance/io/publication.hpp"
#include "native_publication.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace docenhance::tests {
// Exercise codec ownership through the current transaction without inventing processing records.
// Complete product bundles and their validators are exercised by host and executable tests.
struct BinaryPngWriter {
    BinaryPngWriter(image::PlaneView<const std::uint8_t> view, core::Budget& owner,
                    const core::Cancellation& control)
        : image(view), budget(owner), cancellation(control) {}
    image::PlaneView<const std::uint8_t> image;
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const core::Cancellation> cancellation;
    static core::Result<void> write(void* state, const io::BundleSlot& slot) {
        const auto& writer = *static_cast<BinaryPngWriter*>(state);
        return io::write_verified_png(slot, writer.image, writer.budget.get(),
                                      writer.cancellation.get());
    }
};
struct ContinuousPngWriter {
    ContinuousPngWriter(image::RowSource& source, core::Budget& owner,
                        const core::Cancellation& control)
        : rows(source), budget(owner), cancellation(control) {}
    std::reference_wrapper<image::RowSource> rows;
    std::reference_wrapper<core::Budget> budget;
    std::reference_wrapper<const core::Cancellation> cancellation;
    static core::Result<void> write(void* state, const io::BundleSlot& slot) {
        const auto& writer = *static_cast<ContinuousPngWriter*>(state);
        return io::write_verified_png_rows(slot, writer.rows.get(), writer.budget.get(),
                                           writer.cancellation.get());
    }
};
template <typename Writer>
core::Result<std::string> publish_codec_transaction(const std::string& directory, Writer& writer,
                                                    const core::Cancellation& cancellation,
                                                    io::PublishRename commit) {
    const std::array files{
        io::BundleFile{
            .relative = "result.png",
            .state = &writer,
            .write = Writer::write,
        },
    };
    return io::publish_bundle(directory, files, cancellation, commit);
}
inline core::Result<std::string>
publish_png_fixture(const std::string& directory, image::PlaneView<const std::uint8_t> image,
                    core::Budget& budget, const core::Cancellation& cancellation = {},
                    io::PublishRename commit = io::rename_exclusive) {
    BinaryPngWriter writer{image, budget, cancellation};
    return publish_codec_transaction(directory, writer, cancellation, commit);
}
inline core::Result<std::string> publish_png_fixture(const std::string& directory,
                                                     image::RowSource& rows, core::Budget& budget,
                                                     const core::Cancellation& cancellation = {}) {
    ContinuousPngWriter writer{rows, budget, cancellation};
    return publish_codec_transaction(directory, writer, cancellation, io::rename_exclusive);
}
} // namespace docenhance::tests
