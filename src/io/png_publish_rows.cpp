// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/io/continuous_png.hpp"
#include "png_rows.hpp"
#include "publication.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace docenhance::io {
core::Result<std::string> publish_png_rows(const std::string& output_directory,
                                           image::RowSource& rows, core::Budget& budget,
                                           const core::Cancellation& cancellation) {
    struct RowWriter {
        std::reference_wrapper<image::RowSource> rows;
        std::reference_wrapper<core::Budget> budget;
        std::reference_wrapper<const core::Cancellation> cancellation;
    };
    RowWriter state{.rows = rows, .budget = budget, .cancellation = cancellation};
    const PngWriterRef writer{
        .state = &state,
        .write = [](void* raw, const std::filesystem::path& path) -> core::Result<void> {
            auto const& value = *static_cast<RowWriter*>(raw);
            auto encoded = encode_png_rows(path, value.rows.get(), value.budget.get(),
                                           value.cancellation.get());
            if (!encoded) {
                return encoded;
            }
            return verify_png_rows(path, value.rows.get(), value.budget.get(),
                                   value.cancellation.get());
        },
    };
    return publish_generated_png(output_directory, writer, cancellation, rename_exclusive);
}
} // namespace docenhance::io
