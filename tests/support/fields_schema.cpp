// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/core/result.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/source.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "docenhance/record/source_fields.hpp"
#include "fields_fixture.hpp"
#include "method_schema_examples.hpp"
#include "schema_example.hpp"
#include "string_schema_examples.hpp"

#include <cstdio>
#include <exception>
#include <expected>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace docenhance::tests {
namespace {
core::Result<record::Json> packet_refusals(const WirePacket& packet) {
    auto invalid = record::Json::array();
    auto encoded = record::Codec::encode(packet);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    auto changed = *encoded;
    changed.erase("measure");
    invalid.push_back(changed);
    changed = *encoded;
    changed["extra"] = false;
    invalid.push_back(changed);
    for (const auto& field : {"cell", "target"}) {
        changed = *encoded;
        changed[field] = nullptr;
        invalid.push_back(changed);
    }
    changed = *encoded;
    changed["count"] = 256;
    invalid.push_back(changed);
    changed["count"] = true;
    invalid.push_back(changed);
    changed = *encoded;
    changed["identity"] = "not-hex!";
    invalid.push_back(changed);
    changed = *encoded;
    changed["container"] = {{"format", "jpeg"}, {"width", 1}};
    invalid.push_back(changed);
    changed = *encoded;
    changed["mode"] = "unlisted";
    invalid.push_back(changed);
    changed = *encoded;
    changed["samples"] = record::Json::array({0});
    invalid.push_back(changed);
    changed = *encoded;
    changed["measure"] = 1.1;
    invalid.push_back(changed);
    return invalid;
}

core::Result<void> png_source_examples(record::Json& result) {
    record::PngContainer value{.width = 1, .height = 1};
    for (const auto precision : {1U, 2U, 4U, 8U, 16U}) {
        value.precision = precision;
        auto invalid = record::Json::array();
        auto encoded = record::Codec::encode(value);
        if (!encoded) {
            return std::unexpected(std::move(encoded.error()));
        }
        auto hole = *encoded;
        hole["precision"] = 3;
        invalid.push_back(hole);
        hole["precision"] = true;
        invalid.push_back(hole);
        if (auto added = schema_example(result, "PNG source precision " + std::to_string(precision),
                                        value, std::move(invalid));
            !added) {
            return std::unexpected(std::move(added.error()));
        }
    }
    value.precision = 8;
    for (const auto color : {0U, 2U, 3U, 4U, 6U}) {
        value.color_type = color;
        auto invalid = record::Json::array();
        auto encoded = record::Codec::encode(value);
        if (!encoded) {
            return std::unexpected(std::move(encoded.error()));
        }
        for (const auto absent : {1U, 5U}) {
            auto hole = *encoded;
            hole["color_type"] = absent;
            invalid.push_back(hole);
        }
        if (auto added = schema_example(result, "PNG source color " + std::to_string(color), value,
                                        std::move(invalid));
            !added) {
            return std::unexpected(std::move(added.error()));
        }
    }
    return {};
}

core::Result<void> packet_examples(record::Json& result) {
    const WirePacket packet;
    auto invalid_packet = packet_refusals(packet);
    if (!invalid_packet) {
        return std::unexpected(std::move(invalid_packet.error()));
    }
    if (auto added = schema_example(result, "packet", packet, std::move(*invalid_packet)); !added) {
        return std::unexpected(std::move(added.error()));
    }
    WirePacket numeric;
    numeric.cell = 31;
    numeric.target = 0.5;
    numeric.measure = 0.75;
    numeric.container = WireJpeg{.height = 17};
    if (auto added = schema_example(result, "numeric packet", numeric); !added) {
        return std::unexpected(std::move(added.error()));
    }
    if (auto added = schema_example(result, "png", WirePng{}); !added) {
        return std::unexpected(std::move(added.error()));
    }
    if (auto added = schema_example(result, "jpeg", WireJpeg{}); !added) {
        return std::unexpected(std::move(added.error()));
    }
    if (auto added = schema_example(result, "text", WireText{.text = "caf\xc3\xa9"}); !added) {
        return std::unexpected(std::move(added.error()));
    }
    if (auto added = schema_example(result, "light", WireLight{}); !added) {
        return std::unexpected(std::move(added.error()));
    }
    if (auto added = schema_example(result, "noise", WireNoise{}); !added) {
        return std::unexpected(std::move(added.error()));
    }
    const WireStages stages;
    if (auto added =
            schema_example(result, "stages", stages, record::Json::array({record::Json::object()}));
        !added) {
        return std::unexpected(std::move(added.error()));
    }
    return {};
}
core::Result<record::Json> examples() {
    record::Json result = record::Json::array();
    if (auto added = packet_examples(result); !added) {
        return std::unexpected(std::move(added.error()));
    }
    const WireLight light;
    const WireCompleted completed{
        .illumination = light,
        .denoising = std::nullopt,
    };
    auto invalid_completed = record::Json::array();
    invalid_completed.push_back({{"illumination", nullptr}});
    auto completed_encoding = record::Codec::encode(completed);
    if (!completed_encoding) {
        return std::unexpected(std::move(completed_encoding.error()));
    }
    auto wrong_family = std::move(*completed_encoding);
    wrong_family["illumination"]["method"] = "D01";
    invalid_completed.push_back(std::move(wrong_family));
    if (auto added = schema_example(result, "completed", completed, std::move(invalid_completed));
        !added) {
        return std::unexpected(std::move(added.error()));
    }
    if (auto added = schema_example(result, "empty object", WireEmpty{},
                                    record::Json::array({{{"extra", nullptr}}}));
        !added) {
        return std::unexpected(std::move(added.error()));
    }
    using Nullable = std::optional<std::variant<std::monostate, WirePng>>;
    if (auto added = schema_example(result, "nullable variant", Nullable{}); !added) {
        return std::unexpected(std::move(added.error()));
    }
    if (auto added = png_source_examples(result); !added) {
        return std::unexpected(std::move(added.error()));
    }
    if (auto added = method_schema_examples(result); !added) {
        return std::unexpected(std::move(added.error()));
    }
    if (auto added = string_schema_examples(result); !added) {
        return std::unexpected(std::move(added.error()));
    }
    return result;
}
} // namespace
} // namespace docenhance::tests

int main() {
    try {
        const auto examples = docenhance::tests::examples();
        if (!examples) {
            std::cerr << examples.error().message << '\n';
            return 1;
        }
        std::cout << examples->dump() << '\n';
        return std::cout ? 0 : 1;
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputc('\n', stderr));
        return 1;
    } catch (...) {
        static_cast<void>(std::fputs("Unknown descriptor schema failure\n", stderr));
        return 1;
    }
}
