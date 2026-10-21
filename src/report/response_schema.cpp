// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/app/dispatch.hpp"
#include "docenhance/app/options.hpp"
#include "docenhance/app/response.hpp"
#include "docenhance/app/response_fields.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/schema.hpp"
#include "docenhance/report/response.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace docenhance::report {
namespace {
using record::Json;
Json failure_exit_rules() {
    Json alternatives = Json::array();
    for (const auto& [code, identifier] : core::EnumNames<core::ErrorCode>::list) {
        Json error{{"properties", {{"code", {{"const", std::string{identifier}}}}}}};
        Json properties{
            {"error", std::move(error)},
            {
                "exit_code",
                {
                    {
                        "const",
                        static_cast<int>(core::Error{.code = code, .message = {}}.exit_code()),
                    },
                },
            },
        };
        alternatives.push_back(Json{{"properties", std::move(properties)}});
    }
    return alternatives;
}
Json cancellation_rule() {
    const auto cancelled =
        core::Error{.code = core::ErrorCode::cancelled, .message = {}}.identifier();
    Json code{{"const", cancelled}};
    Json error{{"properties", {{"code", std::move(code)}}}};
    Json condition{{"properties", {{"error", std::move(error)}}}};
    Json publication{{"enum", {"not_started", "not_published"}}};
    return Json{
        {"if", std::move(condition)},
        {"then", {{"properties", {{"publication", std::move(publication)}}}}},
    };
}
Json failure_stage_rules() {
    Json rules = Json::array();
    rules.push_back(cancellation_rule());
    constexpr auto families = core::EnumNames<methods::Family>::list;
    for (std::size_t failed = 0; failed < families.size(); ++failed) {
        Json forbidden = Json::array();
        for (std::size_t later = failed; later < families.size(); ++later) {
            forbidden.push_back(std::string{families.at(later).second});
        }
        Json trigger{{"const", std::string{families.at(failed).second}}};
        Json condition{{"properties", {{"failed_stage", std::move(trigger)}}}};
        Json completed{{"propertyNames", {{"not", {{"enum", std::move(forbidden)}}}}}};
        Json consequences{
            {"publication", {{"not", {{"const", "completed"}}}}},
            {"completed", std::move(completed)},
        };
        rules.push_back(Json{
            {"if", std::move(condition)},
            {"then", {{"properties", std::move(consequences)}}},
        });
    }
    return rules;
}
void constrain_failure(Json& schema, app::Command command) {
    schema.update(Json{{"anyOf", failure_exit_rules()}});
    schema.update(Json{{"allOf", failure_stage_rules()}});
    if (command != app::Command::process) {
        auto& properties = schema.at("properties");
        properties.at("publication").update(Json{{"const", "not_started"}});
        properties.at("failed_stage").update(Json{{"const", nullptr}});
        properties.at("completed").update(Json{{"const", Json::object()}});
        properties.at("diagnostics").update(Json{{"const", nullptr}});
        properties.at("warnings").update(Json{{"const", Json::array()}});
    }
}
core::Result<void> constrain_discovery(Json& schema, app::Command command, bool help) {
    const app::Capabilities capabilities{
        .methods = methods::implemented_methods(),
        .input_support = app::input_support,
    };
    app::Outcome outcome{
        .command = command,
        .build = core::build_facts(),
        .payload = app::Methods{.capabilities = capabilities},
    };
    if (help) {
        outcome.payload = app::Help{.capabilities = capabilities};
    } else if (command == app::Command::version) {
        outcome.payload = app::Version{.capabilities = capabilities};
    }
    auto response = response_value(outcome);
    if (!response) {
        return std::unexpected(std::move(response.error()));
    }
    auto actual = encode_response(*response);
    if (!actual) {
        return std::unexpected(std::move(actual.error()));
    }
    auto& properties = schema.at("properties");
    for (const auto& [key, value] : actual->items()) {
        if (key != "command" && key != "schema_version" && key != "version" && key != "exit_code") {
            properties.at(key).update(Json{{"const", value}});
        }
        if (key == "methods" && command == app::Command::methods && !help) {
            auto choices = Json::array({value});
            for (const auto& method : value) {
                choices.push_back(Json::array({method}));
            }
            properties.at(key).erase("const");
            properties.at(key).update(Json{{"enum", std::move(choices)}});
        }
    }
    return {};
}
template <app::Command Command, typename Payload>
core::Result<Json> branch(const app::Response<Command, Payload>* /*type*/) {
    auto result = record::Schema::of<app::Response<Command, Payload>>();
    if (!result) {
        return std::unexpected(std::move(result.error()));
    }
    if constexpr (std::is_same_v<Payload, app::ResponseFailure>) {
        constrain_failure(*result, Command);
    } else if constexpr (std::is_same_v<Payload, app::ResponseHelp> ||
                         std::is_same_v<Payload, app::ResponseCapabilities> ||
                         std::is_same_v<Payload, app::ResponseVersion>) {
        auto constrained =
            constrain_discovery(*result, Command, std::is_same_v<Payload, app::ResponseHelp>);
        if (!constrained) {
            return std::unexpected(std::move(constrained.error()));
        }
    }
    if constexpr (std::is_same_v<Payload, app::ResponseFailure> ||
                  std::is_same_v<Payload, app::ResponsePublished>) {
        result->at("properties").at("warnings").update(Json{{"uniqueItems", true}});
    }
    return result;
}
} // namespace
core::Result<record::Json> response_schema() {
    Json alternatives = Json::array();
    core::Result<void> status;
    const auto append = [&](auto* type) {
        if (!status) {
            return;
        }
        auto schema = branch(type);
        if (!schema) {
            status = std::unexpected(std::move(schema.error()));
            return;
        }
        alternatives.push_back(std::move(*schema));
    };
    [&]<std::size_t... Index>(std::index_sequence<Index...> /*indices*/) {
        (append(static_cast<const std::variant_alternative_t<Index, app::WireResponse>*>(nullptr)),
         ...);
    }(std::make_index_sequence<std::variant_size_v<app::WireResponse>>{});
    if (!status) {
        return std::unexpected(std::move(status.error()));
    }
    return Json{
        {"$schema", "https://json-schema.org/draft/2020-12/schema"},
        {"title", "DocEnhance command response"},
        {"description", "Emitted from actual typed Fields and compiled discovery descriptors."},
        {"oneOf", std::move(alternatives)},
    };
}
} // namespace docenhance::report
