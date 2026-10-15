// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/plane.hpp"
#include "entry_identity.hpp"
#include "native_publication.hpp"

#include <filesystem>
namespace docenhance::fs {
[[nodiscard]] core::Result<void> encode_png(const BundleSlot& slot,
                                            image::PlaneView<const std::uint8_t> view,
                                            core::Budget& budget,
                                            const core::Cancellation& cancellation = {});
[[nodiscard]] core::Result<void> encode_png_rows(const BundleSlot& slot, image::RowSource& source,
                                                 core::Budget& budget,
                                                 const core::Cancellation& cancellation);
[[nodiscard]] core::Result<void> verify_png_rows(const std::filesystem::path& path,
                                                 image::RowSource& source, core::Budget& budget,
                                                 const core::Cancellation& cancellation,
                                                 const EntryLease* owner = nullptr);
[[nodiscard]] core::Result<void> verify_png_image(const std::filesystem::path& path,
                                                  image::PlaneView<const std::uint8_t> image,
                                                  core::Budget& budget,
                                                  const core::Cancellation& cancellation = {},
                                                  const EntryLease* owner = nullptr);
} // namespace docenhance::fs
