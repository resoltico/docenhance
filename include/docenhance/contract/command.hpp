// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace docenhance::contract {
enum class Command : unsigned { root, process, verify, methods, version };
inline constexpr unsigned command_count = 5;
// Which commands an option belongs to, as a typed set rather than a scope string every caller has
// to interpret. spec/cli-contract.json states the scope; tools/generate_spec.py builds the sets.
class CommandSet {
  public:
    constexpr CommandSet() noexcept = default;
    template <typename... Commands>
    constexpr explicit CommandSet(Commands... commands) noexcept
        : bits_((0U | ... | bit(commands))) {}
    [[nodiscard]] constexpr bool contains(Command command) const noexcept {
        return (bits_ & bit(command)) != 0U;
    }
    [[nodiscard]] static constexpr CommandSet all() noexcept {
        CommandSet set;
        set.bits_ = (1U << command_count) - 1U;
        return set;
    }

  private:
    [[nodiscard]] static constexpr unsigned bit(Command command) noexcept {
        const auto index = static_cast<unsigned>(command);
        return index < command_count ? 1U << index : 0U;
    }
    unsigned bits_ = 0;
};
struct InputSupport {
    std::string_view format;
    bool binary = false;
};
struct Invocation {
    Command command = Command::root;
    bool json = false;
    bool help = false;
    bool root_version = false;
    std::string subject;
    std::string output_directory;
    std::optional<std::string> rotate = std::nullopt;
    std::optional<std::string> output_mode = std::nullopt;
    std::optional<std::string> bit_depth = std::nullopt;
    std::optional<std::string> alpha = std::nullopt;
    std::optional<std::string> profile_policy = std::nullopt;
    std::optional<std::string> illumination = std::nullopt;
    std::optional<std::string> background_strength = std::nullopt;
    std::optional<std::string> background_max_gain = std::nullopt;
    std::optional<std::string> background_target = std::nullopt;
    std::optional<std::string> background_radius = std::nullopt;
    std::optional<std::string> background_cell = std::nullopt;
    std::optional<std::string> background_quantile = std::nullopt;
    std::optional<std::string> background_smooth = std::nullopt;
    std::optional<std::string> denoise = std::nullopt;
    std::optional<std::string> denoise_blend = std::nullopt;
    std::optional<std::string> nlm_h = std::nullopt;
    std::optional<std::string> nlm_patch = std::nullopt;
    std::optional<std::string> nlm_search = std::nullopt;
    std::optional<std::string> tv_lambda = std::nullopt;
    std::optional<std::string> tv_iterations = std::nullopt;
    std::optional<std::string> tv_tolerance = std::nullopt;
    std::optional<std::string> deblur = std::nullopt;
    std::optional<std::string> psf = std::nullopt;
    std::optional<std::string> psf_sigma = std::nullopt;
    std::optional<std::string> psf_length = std::nullopt;
    std::optional<std::string> psf_angle = std::nullopt;
    std::optional<std::string> psf_file = std::nullopt;
    std::optional<std::string> wiener_k = std::nullopt;
    std::optional<std::string> deblur_blend = std::nullopt;
    std::optional<std::string> sharpen = std::nullopt;
    std::optional<std::string> sharpen_sigma = std::nullopt;
    std::optional<std::string> sharpen_amount = std::nullopt;
    std::optional<std::string> sharpen_threshold = std::nullopt;
    std::optional<std::string> contrast = std::nullopt;
    std::optional<std::string> contrast_blend = std::nullopt;
    std::optional<std::string> levels_low = std::nullopt;
    std::optional<std::string> levels_high = std::nullopt;
    std::optional<std::string> gamma = std::nullopt;
    std::optional<std::string> clahe_grid = std::nullopt;
    std::optional<std::string> clahe_clip = std::nullopt;
    std::optional<std::string> protect_mask = std::nullopt;
    std::optional<std::string> binarize = std::nullopt;
    std::optional<std::string> fixed_threshold = std::nullopt;
    std::optional<std::string> sauvola_window = std::nullopt;
    std::optional<std::string> sauvola_k = std::nullopt;
    std::optional<std::string> sauvola_r = std::nullopt;
};
[[nodiscard]] constexpr std::string_view command_name(Command command) noexcept {
    switch (command) {
    case Command::root:
        return "root";
    case Command::process:
        return "process";
    case Command::verify:
        return "verify";
    case Command::methods:
        return "methods";
    case Command::version:
        return "version";
    }
    return "root";
}
} // namespace docenhance::contract
