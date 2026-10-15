// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "plan.hpp"

#include "admission.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/parameters.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "parameter_bindings.hpp"

#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
namespace docenhance::app {
namespace {
template <typename Variant, typename Method, typename Parameters>
core::Result<Variant> construct_parameters(core::Result<Method> (*create)(Parameters),
                                           const SuppliedOptions& supplied) {
    auto parameters = bind_parameters<Parameters>(supplied);
    if (!parameters) {
        return std::unexpected(std::move(parameters.error()));
    }
    return create(std::move(*parameters)).transform([](auto method) -> Variant {
        return std::move(method);
    });
}
template <typename Variant, typename Method>
core::Result<Variant> construct_method(const SuppliedOptions& supplied) {
    if constexpr (std::is_same_v<Method, methods::Otsu>) {
        static_cast<void>(supplied);
        return Variant{methods::Otsu::create()};
    } else if constexpr (std::is_same_v<Method, methods::FixedThreshold>) {
        auto parameters = bind_parameters<methods::FixedThresholdParameters>(supplied);
        if (!parameters) {
            return std::unexpected(std::move(parameters.error()));
        }
        return Method::create(parameters->threshold).transform([](auto method) -> Variant {
            return std::move(method);
        });
    } else {
        return construct_parameters<Variant>(&Method::create, supplied);
    }
}
template <typename Variant, std::size_t First, std::size_t... Indices>
core::Result<Variant> select_variant(std::string_view identity, const SuppliedOptions& supplied,
                                     std::index_sequence<Indices...> /*indices*/) {
    std::optional<core::Result<Variant>> selected;
    (
        [&] {
            using Method = std::variant_alternative_t<First + Indices, Variant>;
            if (Method::descriptor().id == identity) {
                selected.emplace(construct_method<Variant, Method>(supplied));
            }
        }(),
        ...);
    if (!selected) {
        return core::failure(core::ErrorCode::invariant,
                             "Selected method has no concrete implementation");
    }
    return std::move(*selected);
}
template <typename Variant, std::size_t First = 1>
core::Result<Variant> select_family(const Invocation& invocation, const SuppliedOptions& supplied,
                                    OptionId selector) {
    const auto& descriptor = option_catalog.at(static_cast<std::size_t>(selector));
    if (!descriptor.selector) {
        return binding_error();
    }
    const auto identity = selected_method(invocation, descriptor);
    if constexpr (First != 0) {
        if (identity.empty() && effective_value(invocation, selector) == "off") {
            return Variant{std::variant_alternative_t<0, Variant>{}};
        }
    }
    return select_variant<Variant, First>(
        identity, supplied, std::make_index_sequence<std::variant_size_v<Variant> - First>{});
}
template <typename Value, std::size_t Count>
core::Result<Value>
output_policy(const Invocation& invocation, OptionId id,
              const std::array<std::pair<std::string_view, Value>, Count>& map) {
    const auto selected = effective_value(invocation, id);
    for (const auto& [spelling, policy] : map) {
        if (selected == spelling) {
            return policy;
        }
    }
    return core::failure(core::ErrorCode::invariant, "Parsed option has no concrete output policy");
}
core::Result<image::QuarterTurn> rotation(const Invocation& invocation) {
    constexpr auto degrees = std::to_array<std::pair<std::string_view, unsigned>>({
        {"0", 0},
        {"90", image::QuarterTurn::quarter_degrees},
        {"180", image::QuarterTurn::half_degrees},
        {"270", image::QuarterTurn::three_quarter_degrees},
    });
    const auto selected = output_policy(invocation, OptionId::rotate, degrees);
    if (!selected) {
        return std::unexpected(selected.error());
    }
    const auto value = image::QuarterTurn::from_degrees(*selected);
    return value ? core::Result<image::QuarterTurn>{*value}
                 : core::Result<image::QuarterTurn>{binding_error()};
}
core::Result<image::Continuous> continuous(const Invocation& invocation) {
    using image::AlphaPolicy;
    using image::OutputDepth;
    using image::ProfilePolicy;
    using image::ToneMode;
    constexpr auto modes = std::to_array<std::pair<std::string_view, ToneMode>>({
        {"preserve", ToneMode::preserve},
        {"gray", ToneMode::gray},
    });
    constexpr auto depths = std::to_array<std::pair<std::string_view, OutputDepth>>({
        {"auto", OutputDepth::automatic},
        {"8", OutputDepth::byte},
        {"16", OutputDepth::word},
    });
    constexpr auto alphas = std::to_array<std::pair<std::string_view, AlphaPolicy>>({
        {"white", AlphaPolicy::white},
        {"black", AlphaPolicy::black},
        {"reject", AlphaPolicy::reject},
    });
    constexpr auto profiles = std::to_array<std::pair<std::string_view, ProfilePolicy>>({
        {"embedded", ProfilePolicy::embedded},
        {"srgb", ProfilePolicy::srgb},
    });
    const auto mode = output_policy(invocation, OptionId::output_mode, modes);
    if (!mode) {
        return std::unexpected(mode.error());
    }
    const auto depth = output_policy(invocation, OptionId::bit_depth, depths);
    if (!depth) {
        return std::unexpected(depth.error());
    }
    const auto alpha = output_policy(invocation, OptionId::alpha, alphas);
    if (!alpha) {
        return std::unexpected(alpha.error());
    }
    const auto profile = output_policy(invocation, OptionId::profile_policy, profiles);
    if (!profile) {
        return std::unexpected(profile.error());
    }
    return image::Continuous::create(
        {.mode = *mode, .depth = *depth, .alpha = *alpha, .profile = *profile});
}
core::Result<PhotometricPlan> photometric(const Invocation& invocation,
                                          const SuppliedOptions& supplied) {
    auto illumination =
        select_family<methods::Illumination>(invocation, supplied, OptionId::illumination);
    if (!illumination) {
        return std::unexpected(std::move(illumination.error()));
    }
    auto denoising = select_family<methods::Denoising>(invocation, supplied, OptionId::denoise);
    if (!denoising) {
        return std::unexpected(std::move(denoising.error()));
    }
    auto restoration = select_family<methods::Restoration>(invocation, supplied, OptionId::deblur);
    if (!restoration) {
        return std::unexpected(std::move(restoration.error()));
    }
    auto contrast = select_family<methods::Contrast>(invocation, supplied, OptionId::contrast);
    if (!contrast) {
        return std::unexpected(std::move(contrast.error()));
    }
    auto sharpening = select_family<methods::Sharpening>(invocation, supplied, OptionId::sharpen);
    if (!sharpening) {
        return std::unexpected(std::move(sharpening.error()));
    }
    return PhotometricPlan{
        .illumination = *illumination,
        .denoising = *denoising,
        .restoration = std::move(*restoration),
        .contrast = *contrast,
        .sharpening = *sharpening,
    };
}
} // namespace
core::Result<ProcessPlan> prepare_plan(const Invocation& invocation,
                                       const SuppliedOptions& supplied) {
    const auto turn = rotation(invocation);
    if (!turn) {
        return std::unexpected(turn.error());
    }
    if (effective_value(invocation, OptionId::output_mode) == "bw") {
        auto method =
            select_family<methods::Binarization, 0>(invocation, supplied, OptionId::binarize);
        if (!method) {
            return std::unexpected(std::move(method.error()));
        }
        return BinaryPlan{
            .operation = *method,
            .rotation = *turn,
        };
    }
    const auto tone = continuous(invocation);
    if (!tone) {
        return std::unexpected(tone.error());
    }
    auto stages = photometric(invocation, supplied);
    if (!stages) {
        return std::unexpected(std::move(stages.error()));
    }
    return ContinuousPlan{
        .operation = *tone,
        .photometric = std::move(*stages),
        .protection = invocation.option(OptionId::protect_mask),
        .rotation = *turn,
    };
}
} // namespace docenhance::app
