// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/schema.hpp"
#include "docenhance/report/response.hpp"

#include <exception>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {
using docenhance::record::Json;
void object_tokens(const Json& schema, std::set<std::string>& tokens) {
    if (const auto properties = schema.find("properties");
        properties != schema.end() && properties->is_object()) {
        for (const auto& [name, description] : properties->items()) {
            static_cast<void>(description);
            tokens.insert(Json(name).dump());
        }
    }
    if (const auto literal = schema.find("const");
        literal != schema.end() && literal->is_primitive()) {
        tokens.insert(literal->dump());
    }
    if (const auto choices = schema.find("enum"); choices != schema.end() && choices->is_array()) {
        for (const auto& choice : *choices) {
            if (choice.is_primitive()) {
                tokens.insert(choice.dump());
            }
        }
    }
}
void record_tokens(const Json& schema, std::set<std::string>& tokens) {
    std::vector<const Json*> pending{&schema};
    while (!pending.empty()) {
        const auto* const node = pending.back();
        pending.pop_back();
        if (node->is_object()) {
            object_tokens(*node, tokens);
        }
        if (node->is_object() || node->is_array()) {
            for (auto child = node->crbegin(); child != node->crend(); ++child) {
                pending.push_back(&*child);
            }
        }
    }
}
std::string record_dictionary(const Json& schema) {
    std::set<std::string> tokens;
    record_tokens(schema, tokens);
    std::string text = "# Generated from actual native run-record Fields.\n";
    for (const auto& token : tokens) {
        text += Json(token).dump() + "\n";
    }
    return text;
}
} // namespace
int main() {
    try {
        auto run = docenhance::record::Schema::of<docenhance::record::RunRecord>();
        if (!run) {
            std::cerr << run.error().identifier() << ": " << run.error().message << '\n';
            return 1;
        }
        auto response = docenhance::report::response_schema();
        if (!response) {
            std::cerr << response.error().identifier() << ": " << response.error().message << '\n';
            return 1;
        }
        run->update(Json{
            {"$schema", "https://json-schema.org/draft/2020-12/schema"},
            {"title", "DocEnhance run record"},
            {"description", "Emitted from actual typed run-record Fields."},
        });
        auto dictionary = record_dictionary(*run);
        const docenhance::record::Json result{
            {"command-response.schema.json", std::move(*response)},
            {"run-record.schema.json", std::move(*run)},
            {"record.dict", std::move(dictionary)},
        };
        std::cout << result.dump() << '\n';
        std::cout.flush();
        return std::cout ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "Native schema emission failed\n";
        return 1;
    }
}
