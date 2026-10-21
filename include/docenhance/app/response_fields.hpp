// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/response.hpp"
#include "docenhance/core/execution_limits.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/version.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace docenhance {
template <> struct core::EnumNames<core::Publication> {
    static constexpr std::array list{
        std::pair{core::Publication::not_started, std::string_view{"not_started"}},
        std::pair{core::Publication::not_published, std::string_view{"not_published"}},
        std::pair{core::Publication::completed, std::string_view{"completed"}},
        std::pair{core::Publication::unknown, std::string_view{"unknown"}},
    };
};
template <> struct core::EnumNames<core::ErrorCode> {
    static constexpr auto list = [] {
        constexpr auto count = static_cast<std::size_t>(core::ErrorCode::invariant) + 1;
        std::array<std::pair<core::ErrorCode, std::string_view>, count> result{};
        for (std::size_t i = 0; i < count; ++i) {
            const auto code = static_cast<core::ErrorCode>(i);
            result.at(i) = {code, core::Error{.code = code, .message = {}}.identifier()};
        }
        return result;
    }();
};
template <> struct core::Fields<app::ResponseParameter> {
    using Value = app::ResponseParameter;
    static constexpr auto list = std::tuple{
        core::field("option", &Value::option, core::Text{}),
        core::field("kind", &Value::kind, core::Text{}),
        core::field("domain", &Value::domain, core::Text{}),
        core::field("description", &Value::description, core::Text{}),
    };
};
template <> struct core::Fields<app::ResponseMethod> {
    using Value = app::ResponseMethod;
    static constexpr auto list = std::tuple{
        core::field("id", &Value::id, core::Text{}),
        core::field("method_version", &Value::method_version),
        core::field("name", &Value::name, core::Text{}),
        core::field("family", &Value::family, core::Text{}),
        core::field("selectors", &Value::selectors),
        core::field("parameters", &Value::parameters),
    };
};
template <> struct core::Fields<app::ResponseInput> {
    using Value = app::ResponseInput;
    static constexpr auto list = std::tuple{
        core::field("format", &Value::format, core::Text{}),
        core::field("output_modes", &Value::output_modes),
    };
};
template <> struct core::Fields<app::ResponseCapabilities> {
    using Value = app::ResponseCapabilities;
    static constexpr auto list = std::tuple{
        core::field("methods", &Value::methods),
        core::field("supported_formats", &Value::supported_formats),
        core::field("input_support", &Value::input_support),
    };
};
template <> struct core::Fields<app::ResponseOption> {
    using Value = app::ResponseOption;
    static constexpr auto list = std::tuple{
        core::field("name", &Value::name, core::Text{}),
        core::field("metavar", &Value::metavar),
        core::field("description", &Value::description, core::Text{}),
        core::field("domain", &Value::domain),
        core::field("methods", &Value::methods),
    };
};
template <> struct core::Fields<app::ResponseHelp> {
    using Value = app::ResponseHelp;
    static constexpr auto list = std::tuple{
        core::field("usage", &Value::usage, core::Text{}),
        core::field("options", &Value::options),
    };
};
template <> struct core::Fields<app::Diagnostics> {
    using Value = app::Diagnostics;
    static constexpr auto list = std::tuple{
        core::field("threads", &Value::threads,
                    core::Range<std::uint32_t>{
                        .minimum = core::min_execution_threads,
                        .maximum = core::max_execution_threads,
                    }),
        core::field("memory_limit_mib", &Value::memory_limit_mib,
                    core::Range<std::uint64_t>{
                        .minimum = core::min_memory_limit_mib,
                        .maximum = core::max_memory_limit_mib,
                    }),
        core::field("charged_peak_bytes", &Value::charged_peak_bytes,
                    core::Range<std::uint64_t>{
                        .minimum = 0,
                        .maximum = core::max_memory_limit_mib * (std::uint64_t{1024} * 1024),
                    }),
    };
};
template <> struct core::Fields<app::ResponsePublished> {
    using Value = app::ResponsePublished;
    static constexpr auto list = std::tuple{
        core::field("publication", &Value::publication, core::Exact{core::Publication::completed}),
        core::field("output", &Value::output, core::Text{}),
        core::field("record_sha256", &Value::record_sha256,
                    core::Hex{.digits = core::sha256_hex_length}),
        core::field("record", &Value::record),
        core::field("diagnostics", &Value::diagnostics),
        core::field("warnings", &Value::warnings,
                    core::Range<std::size_t>{
                        .minimum = 0,
                        .maximum = core::EnumNames<core::Warning>::list.size(),
                    }),
    };
};
template <> struct core::Fields<app::ResponseConfirmed> {
    using Value = app::ResponseConfirmed;
    static constexpr auto list = std::tuple{
        core::field("path", &Value::path, core::BundlePath{}),
        core::field("sha256", &Value::sha256, core::Hex{.digits = core::sha256_hex_length}),
        core::field(
            "bytes", &Value::bytes,
            core::Range<std::uint64_t>{.minimum = 1, .maximum = core::bundle_max_file_bytes}),
    };
};
template <> struct core::Fields<app::ResponseVerified> {
    using Value = app::ResponseVerified;
    static constexpr auto list = std::tuple{
        core::field("directory", &Value::directory, core::Text{}),
        core::field("run", &Value::run, core::Hex{.digits = core::run_identity_hex_length}),
        core::field("recorded", &Value::recorded, core::Instant{}),
        core::field(
            "confirmed", &Value::confirmed,
            core::Range<std::size_t>{.minimum = 2, .maximum = app::response_confirmed_limit}),
        core::field("establishes", &Value::establishes,
                    core::Literal{"artifacts_agree_with_record"}),
    };
};
template <> struct core::Fields<app::ResponseError> {
    using Value = app::ResponseError;
    static constexpr auto list = std::tuple{
        core::field("code", &Value::code),
        core::field("message", &Value::message, core::Text{}),
    };
};
template <> struct core::Fields<app::ResponseFailure> {
    using Value = app::ResponseFailure;
    static constexpr auto list = std::tuple{
        core::field("error", &Value::error),
        core::field("publication", &Value::publication),
        core::field("failed_stage", &Value::failed_stage),
        core::field("completed", &Value::completed),
        core::field("diagnostics", &Value::diagnostics),
        core::field("warnings", &Value::warnings,
                    core::Range<std::size_t>{
                        .minimum = 0,
                        .maximum = core::EnumNames<core::Warning>::list.size(),
                    }),
    };
};
template <> struct core::Fields<app::ResponseVersion> {
    using Value = app::ResponseVersion;
    static constexpr auto list =
        std::tuple_cat(core::Fields<app::ResponseCapabilities>::list,
                       std::tuple{
                           core::field("dependency_lock_sha256", &Value::dependency_lock_sha256,
                                       core::Hex{.digits = core::sha256_hex_length}),
                           core::field("platform", &Value::platform, core::Text{}),
                           core::field("compiler", &Value::compiler, core::Text{}),
                       });
};
template <app::Command Command, typename Payload>
struct core::WireTag<app::Response<Command, Payload>> {
    static constexpr std::string_view key = "command";
    static constexpr std::string_view value = app::command_name(Command);
};
template <app::Command Command, typename Payload>
struct core::Fields<app::Response<Command, Payload>> {
    using Value = app::Response<Command, Payload>;
    static constexpr auto list = [] {
        const auto header = std::tuple{
            core::field("schema_version", &Value::schema_version,
                        core::Exact{app::response_schema_version}),
            core::field("version", &Value::version, core::Literal{application_version}),
        };
        if constexpr (std::is_same_v<Payload, app::ResponseFailure>) {
            return std::tuple_cat(header, std::tuple{core::field("exit_code", &Value::exit_code)},
                                  core::Fields<Payload>::list);
        } else {
            return std::tuple_cat(
                header, std::tuple{core::field("exit_code", &Value::exit_code, core::Exact{0})},
                core::Fields<Payload>::list);
        }
    }();
};
} // namespace docenhance
