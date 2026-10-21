// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/plane.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/working.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
namespace docenhance::methods {
inline constexpr std::uint32_t nlm_band_rows = 64;
inline constexpr std::uint32_t nlm_checkpoint_samples = 256;
inline constexpr std::uint32_t nlm_observation_rows = 3;
struct NlmBand {
    std::uint32_t first;
    std::uint32_t rows;
};
struct NlmOffset {
    std::int32_t x;
    std::int32_t y;
};
inline std::uint32_t nlm_band_count(std::uint32_t height) noexcept {
    return (height / nlm_band_rows) + static_cast<std::uint32_t>(height % nlm_band_rows != 0);
}
inline NlmBand nlm_band(std::uint32_t height, std::uint32_t index) noexcept {
    const auto first = index * nlm_band_rows;
    return {.first = first, .rows = std::min(nlm_band_rows, height - first)};
}
template <typename Sample>
core::Result<image::Plane<Sample>> nlm_allocate(Working working, std::uint32_t width,
                                                std::uint32_t height) {
    if (working.scheduler.get().cancellation().requested(core::Checkpoint::allocation)) {
        return core::cancelled();
    }
    return image::Plane<Sample>::allocate(working.budget.get(), width, height);
}
struct NlmPrepared {
    image::Plane<std::uint16_t> codes;
    image::Plane<double> weights;
    image::Plane<std::uint32_t> xfold;
    image::Plane<std::uint32_t> yfold;
    image::Plane<std::uint64_t> observations;
};
struct NlmBandStorage {
    image::Plane<double> accumulation;
    image::Plane<std::uint32_t> horizontal;
    image::Plane<std::uint32_t> differences;
    image::Plane<std::uint32_t> columns;
};
struct NlmContext {
    NlmContext(const Nlm& value, Working frame, const NlmPrepared& field) noexcept
        : method(value), working(frame), prepared(field) {}
    NlmContext(const Nlm&&, Working, const NlmPrepared&) = delete;
    NlmContext(const Nlm&, Working, const NlmPrepared&&) = delete;
    std::reference_wrapper<const Nlm> method;
    Working working;
    std::reference_wrapper<const NlmPrepared> prepared;
};
struct NlmObservation {
    std::uint64_t evaluated{};
    std::uint64_t corrected{};
    std::uint64_t changed{};
};
[[nodiscard]] core::Result<NlmPrepared> prepare_nlm(const Nlm& method, Working working);
[[nodiscard]] core::Result<NlmBandStorage> nlm_storage(const NlmContext& context, NlmBand band);
[[nodiscard]] core::Result<void> nlm_distances(const NlmContext& context, NlmBand band,
                                               NlmOffset offset, NlmBandStorage& storage);
[[nodiscard]] core::Result<void> nlm_accumulate(const NlmContext& context, NlmBand band,
                                                NlmOffset offset, NlmBandStorage& storage);
[[nodiscard]] core::Result<NlmObservation> nlm_map(const NlmContext& context, NlmBand band,
                                                   const NlmBandStorage& storage);
} // namespace docenhance::methods
