// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/core/fields.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/numeric.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/method_catalog.hpp"
#include "docenhance/methods/parameters.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>
namespace docenhance::methods {
class Nlm {
  public:
    [[nodiscard]] static core::Result<Nlm> create(NlmParameters parameters = {});
    [[nodiscard]] NlmParameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return nlm_descriptor;
    }

  private:
    explicit Nlm(NlmParameters parameters) : parameters_(parameters) {}
    NlmParameters parameters_;
};
struct DenoisingOff {};
class Tvl1 {
  public:
    [[nodiscard]] static core::Result<Tvl1> create(Tvl1Parameters parameters = {});
    [[nodiscard]] Tvl1Parameters parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] static constexpr ImplementedMethod descriptor() noexcept {
        return tvl1_descriptor;
    }

  private:
    explicit Tvl1(Tvl1Parameters parameters) : parameters_(parameters) {}
    Tvl1Parameters parameters_;
};
using Denoising = std::variant<DenoisingOff, Nlm, Tvl1>;
enum class Tvl1Stop { tolerance_met, iteration_limit };
[[nodiscard]] core::Result<std::uint16_t> nlm_quantize(const image::Rgb& rgb);
[[nodiscard]] core::Result<image::Rgb> nlm_correct(const image::Rgb& rgb, std::uint16_t input,
                                                   std::uint16_t output, double blend);
} // namespace docenhance::methods

namespace docenhance {
template <> struct core::EnumNames<methods::Tvl1Stop> {
    static constexpr std::array list{
        std::pair{methods::Tvl1Stop::tolerance_met, std::string_view{"tolerance_met"}},
        std::pair{methods::Tvl1Stop::iteration_limit, std::string_view{"iteration_limit"}},
    };
};
} // namespace docenhance
