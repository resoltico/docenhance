// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "summary.hpp"

#include "docenhance/app/process.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/raster.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/json.hpp"
#include "docenhance/record/record.hpp"
#include "docenhance/record/scalar.hpp"
#include "docenhance/report/warnings.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
namespace docenhance::report {
namespace {
void append_report(std::string& text, const record::Json& report) {
    struct Frame {
        record::Json::const_iterator next;
        record::Json::const_iterator end;
        std::size_t indentation;
    };
    std::vector<Frame> stack{{.next = report.cbegin(), .end = report.cend(), .indentation = 2}};
    while (!stack.empty()) {
        auto& frame = stack.back();
        if (frame.next == frame.end) {
            stack.pop_back();
            continue;
        }
        const auto item = frame.next++;
        const auto indentation = frame.indentation;
        const auto& value = item.value();
        text.append(indentation, ' ');
        text += item.key() + ':';
        if (value.is_object() && !value.empty()) {
            text += '\n';
            stack.push_back(
                {.next = value.cbegin(), .end = value.cend(), .indentation = indentation + 2});
        } else {
            text += value.is_null() ? " not reported\n" : " " + value.dump() + "\n";
        }
    }
}
core::Result<std::string> interpretation_text(const record::RunRecord& value) {
    const auto& facts = value.execution.interpretation;
    auto alpha =
        std::visit([](const auto& output) { return record::ScalarCodec::encode(output.alpha); },
                   value.request.output);
    if (!alpha) {
        return std::unexpected(std::move(alpha.error()));
    }
    std::string text = std::string{"Interpretation: transfer="} +
                       (facts.assumed_transfer ? "assumed" : "declared") +
                       "; primaries=" + (facts.assumed_primaries ? "assumed" : "declared") +
                       "; orientation=" + std::to_string(facts.orientation.code()) +
                       "; rotation=" + std::to_string(facts.rotation.degrees()) + "\n";
    if (facts.flattened_pixels != 0) {
        text += "Alpha: " + std::to_string(facts.flattened_pixels) +
                " non-opaque pixels composited over " + alpha->get<std::string>() +
                "; original alpha cannot be recovered\n";
    }
    if (facts.depth_reduced) {
        text += "Precision: " + std::to_string(facts.decoded.depth.bits()) +
                "-bit decoded samples reduced to " + std::to_string(facts.output.depth.bits()) +
                " bits\n";
    }
    return text;
}
} // namespace
core::Result<std::string> warnings_text(const RunWarnings& warnings) {
    std::string text;
    for (const auto warning : warnings) {
        if (!warning) {
            continue;
        }
        auto name = record::ScalarCodec::encode(*warning);
        if (!name) {
            return std::unexpected(std::move(name.error()));
        }
        const auto explanation = warning_explanation(*warning);
        if (explanation) {
            text += name->get<std::string>();
            text += ": ";
            text += *explanation;
            text += '\n';
        }
    }
    return text;
}
core::Result<std::string> completed_text(const methods::StageReports& facts) {
    if (!methods::validate(facts)) {
        return core::failure(core::ErrorCode::invariant, "Invalid completed stage facts");
    }
    auto encoded = record::Codec::encode<methods::StageReports>(facts);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    std::string text;
    const auto append = [&](const auto& field) {
        const auto& value = encoded->at(std::string{field.key});
        // An absent slot may be disabled or unstarted; neither is a completion claim.
        if (!value.is_null()) {
            text += std::string{field.key} + ":\n";
            append_report(text, value);
        }
    };
    std::apply([&](auto... field) { (append(field), ...); },
               core::Fields<methods::StageReports>::list);
    return text.empty() ? "Completed stage facts: none reported\n"
                        : "Completed stage facts:\n" + text;
}
core::Result<std::string> diagnostics_text(const app::Diagnostics& diagnostics) {
    return "Execution: threads=" + std::to_string(diagnostics.threads) +
           "; memory limit=" + std::to_string(diagnostics.memory_limit_mib) +
           " MiB; charged peak=" + std::to_string(diagnostics.charged_peak_bytes) + " bytes\n";
}
core::Result<std::string> published_text(const app::Published& value) {
    const auto& interpretation = value.record.execution.interpretation;
    std::string text = "Wrote verified PNG: " + value.output + "\n";
    text += std::to_string(interpretation.output.depth.bits()) + " bits; " +
            std::to_string(image::components(interpretation.output.model)) + " channels\n";
    text += "run " + value.record.context.identity + " recorded " + value.record.context.recorded +
            "\n";
    text += "record SHA-256 " + value.record_identity.sha256 + "\n";
    auto interpreted = interpretation_text(value.record);
    if (!interpreted) {
        return std::unexpected(std::move(interpreted.error()));
    }
    text += *interpreted;
    auto completed = completed_text(value.record.execution);
    if (!completed) {
        return std::unexpected(std::move(completed.error()));
    }
    text += *completed;
    auto diagnostics = diagnostics_text(value.diagnostics);
    if (!diagnostics) {
        return std::unexpected(std::move(diagnostics.error()));
    }
    text += *diagnostics;
    auto warnings = warnings_text(run_warnings(value.record));
    if (!warnings) {
        return std::unexpected(std::move(warnings.error()));
    }
    text += *warnings;
    return text;
}
} // namespace docenhance::report
