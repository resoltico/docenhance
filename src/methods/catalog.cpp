// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/methods/catalog.hpp"

#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/reviewed_methods.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <utility>
#include <variant>
namespace docenhance::methods {
namespace {
template <std::size_t... Lights, std::size_t... Denoisers, std::size_t... Contrasts,
          std::size_t... Sharpens, std::size_t... Indices>
constexpr auto executable_catalog(std::index_sequence<Lights...> /*lights*/,
                                  std::index_sequence<Denoisers...> /*denoisers*/,
                                  std::index_sequence<Contrasts...> /*contrasts*/,
                                  std::index_sequence<Sharpens...> /*sharpens*/,
                                  std::index_sequence<Indices...> /*indices*/) noexcept {
    return std::array{std::variant_alternative_t<Lights + 1, Illumination>::descriptor()...,
                      std::variant_alternative_t<Denoisers + 1, Denoising>::descriptor()...,
                      std::variant_alternative_t<Contrasts + 1, Contrast>::descriptor()...,
                      std::variant_alternative_t<Sharpens + 1, Sharpening>::descriptor()...,
                      std::variant_alternative_t<Indices, Binarization>::descriptor()...};
}
constexpr auto illumination_indices =
    std::make_index_sequence<std::variant_size_v<Illumination> - 1>{};
constexpr auto binarization_indices = std::make_index_sequence<std::variant_size_v<Binarization>>{};
constexpr auto denoising_indices = std::make_index_sequence<std::variant_size_v<Denoising> - 1>{};
constexpr auto contrast_indices = std::make_index_sequence<std::variant_size_v<Contrast> - 1>{};
constexpr auto sharpening_indices = std::make_index_sequence<std::variant_size_v<Sharpening> - 1>{};
constexpr auto catalog =
    executable_catalog(illumination_indices, denoising_indices, contrast_indices,
                       sharpening_indices, binarization_indices);
static_assert(std::ranges::equal(catalog, reviewed_methods),
              "Reviewed capabilities and executable method types must agree");
} // namespace
std::span<const ImplementedMethod> implemented_methods() noexcept {
    return catalog;
}
} // namespace docenhance::methods
