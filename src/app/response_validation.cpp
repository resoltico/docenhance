// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/options.hpp"
#include "docenhance/app/process.hpp"
#include "docenhance/app/response.hpp"
#include "docenhance/core/bundle_path.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/limits.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/core/warnings.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/record/record.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace docenhance::app {
namespace {
bool method_matches(const ResponseMethod& value, const methods::ImplementedMethod& actual) {
    if (value.id != actual.id || value.method_version != actual.method_version ||
        value.name != actual.name || value.family != actual.family ||
        !std::ranges::equal(value.selectors, actual.selectors) ||
        value.parameters.size() != actual.parameters.size()) {
        return false;
    }
    return std::ranges::equal(
        value.parameters, actual.parameters, [](const auto& parameter, const auto& source) {
            return parameter.option == source.option && parameter.kind == source.kind &&
                   parameter.domain == source.domain && parameter.description == source.description;
        });
}

bool warnings_valid(const std::vector<core::Warning>& value) {
    std::optional<std::uint8_t> previous;
    for (const auto warning : value) {
        const auto index = std::to_underlying(warning);
        if (index >= core::EnumNames<core::Warning>::list.size() ||
            (previous && index <= *previous)) {
            return false;
        }
        previous = index;
    }
    return true;
}
bool diagnostics_valid(const Diagnostics& value) {
    constexpr auto mib = std::uint64_t{1024} * 1024;
    return value.threads >= core::min_execution_threads &&
           value.threads <= core::max_execution_threads &&
           value.memory_limit_mib >= core::min_memory_limit_mib &&
           value.memory_limit_mib <= core::max_memory_limit_mib &&
           value.charged_peak_bytes <= value.memory_limit_mib * mib;
}
} // namespace
bool validate(const ResponseCapabilities& value) {
    const auto actual = methods::implemented_methods();
    if ((value.methods.size() != actual.size() && value.methods.size() != 1) ||
        value.supported_formats.size() != input_support.size() ||
        value.input_support.size() != input_support.size()) {
        return false;
    }
    if (value.methods.size() == 1) {
        if (!std::ranges::any_of(actual, [&](const auto& method) {
                return method_matches(value.methods.front(), method);
            })) {
            return false;
        }
    } else if (!std::ranges::equal(value.methods, actual, method_matches)) {
        return false;
    }
    constexpr std::size_t binary_modes = 3;
    constexpr std::size_t continuous_modes = 2;
    for (std::size_t i = 0; i < input_support.size(); ++i) {
        const auto& support = value.input_support.at(i);
        const auto& modes = support.output_modes;
        if (value.supported_formats.at(i) != input_support.at(i).format ||
            support.format != input_support.at(i).format ||
            modes.size() != (input_support.at(i).binary ? binary_modes : continuous_modes) ||
            modes.at(0) != "preserve" || modes.at(1) != "gray" ||
            (input_support.at(i).binary && modes.at(2) != "bw")) {
            return false;
        }
    }
    return true;
}
bool validate(const ResponseVersion& value) {
    const auto build = core::build_facts();
    return value.methods.size() == methods::implemented_methods().size() &&
           validate(static_cast<const ResponseCapabilities&>(value)) &&
           value.dependency_lock_sha256 == build.dependency_lock_sha256 &&
           value.platform == build.platform && value.compiler == build.compiler;
}
bool valid_response_help(const ResponseHelp& value, Command command) {
    if (value.usage != command_usage(command)) {
        return false;
    }
    std::size_t index = 0;
    for (const auto& source : option_catalog) {
        if (!source.scope.contains(command)) {
            continue;
        }
        if (index == value.options.size()) {
            return false;
        }
        const auto& item = value.options.at(index++);
        if (item.name != source.name || item.metavar != source.metavar ||
            item.description != source.description || item.domain != source.domain ||
            item.methods != source.methods) {
            return false;
        }
    }
    return index == value.options.size();
}
bool validate(const ResponsePublished& value) {
    return value.publication == core::Publication::completed && core::valid_path(value.output) &&
           core::valid_hexadecimal(value.record_sha256, core::sha256_hex_length) &&
           record::validate(value.record) && diagnostics_valid(value.diagnostics) &&
           value.diagnostics.charged_peak_bytes != 0 && warnings_valid(value.warnings);
}
namespace {
std::uint64_t confirmed_limit(std::string_view path) {
    if (path == record::record_name) {
        return record::record_max_bytes;
    }
    if (path == record::psf_name) {
        return core::psf_encoded_bytes_max;
    }
    return path == record::image_name || path == record::mask_name ? core::bundle_max_file_bytes
                                                                   : 0;
}
} // namespace
bool validate(const ResponseVerified& value) {
    if (!core::valid_path(value.directory) ||
        !core::valid_hexadecimal(value.run, core::run_identity_hex_length) ||
        !core::valid_instant(value.recorded) || value.confirmed.size() < 2 ||
        value.confirmed.size() > response_confirmed_limit ||
        value.establishes != "artifacts_agree_with_record") {
        return false;
    }
    for (std::size_t i = 0; i < value.confirmed.size(); ++i) {
        const auto& entry = value.confirmed.at(i);
        if (!core::valid_bundle_path(entry.path) || entry.bytes == 0 ||
            entry.bytes > confirmed_limit(entry.path) ||
            !core::valid_hexadecimal(entry.sha256, core::sha256_hex_length)) {
            return false;
        }
        for (std::size_t prior = 0; prior < i; ++prior) {
            if (value.confirmed.at(prior).path == entry.path) {
                return false;
            }
        }
    }
    return std::ranges::any_of(
               value.confirmed,
               [](const auto& entry) { return entry.path == record::image_name; }) &&
           std::ranges::any_of(value.confirmed,
                               [](const auto& entry) { return entry.path == record::record_name; });
}
bool validate(const ResponseFailure& value) {
    if (value.error.code == core::ErrorCode::cancelled &&
        value.publication != core::Publication::not_started &&
        value.publication != core::Publication::not_published) {
        return false;
    }
    if (!methods::validate(value.completed) || !warnings_valid(value.warnings) ||
        (value.diagnostics && !diagnostics_valid(*value.diagnostics))) {
        return false;
    }
    if (value.publication == core::Publication::completed && value.failed_stage) {
        return false;
    }
    if (!value.failed_stage) {
        return true;
    }
    const auto& reports = value.completed;
    const std::array done{
        reports.illumination.has_value(), reports.denoising.has_value(),
        reports.restoration.has_value(),  reports.contrast.has_value(),
        reports.sharpening.has_value(),   reports.binarization.has_value(),
    };
    const auto failed = static_cast<std::size_t>(*value.failed_stage);
    if (failed >= done.size()) {
        return false;
    }
    for (std::size_t i = failed; i < done.size(); ++i) {
        if (done.at(i)) {
            return false;
        }
    }
    return true;
}
} // namespace docenhance::app
