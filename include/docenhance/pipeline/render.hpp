// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/codec/encoded_png.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/memory.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/exec/scheduler.hpp"
#include "docenhance/image/output.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/report_values.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/methods/warnings.hpp"

#include <expected>
#include <optional>
#include <utility>

namespace docenhance::pipeline {
struct EncodedInputs {
    core::Buffer source;
    core::ContentIdentity source_identity;
    core::Buffer mask;
    std::optional<core::ContentIdentity> mask_identity;
    core::Buffer psf;
    std::optional<core::ContentIdentity> psf_identity;
};
class Input {
  public:
    Input() noexcept = default;
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    Input(Input&& other) noexcept
        : encoded_{
              .source = std::move(other.encoded_.source),
              .source_identity = std::exchange(other.encoded_.source_identity, {}),
              .mask = std::move(other.encoded_.mask),
              .mask_identity = std::exchange(other.encoded_.mask_identity, std::nullopt),
              .psf = std::move(other.encoded_.psf),
              .psf_identity = std::exchange(other.encoded_.psf_identity, std::nullopt),
          },
          ready_(std::exchange(other.ready_, false)) {}
    Input& operator=(Input&& other) noexcept {
        if (this != &other) {
            encoded_.source = std::move(other.encoded_.source);
            encoded_.source_identity = std::exchange(other.encoded_.source_identity, {});
            encoded_.mask = std::move(other.encoded_.mask);
            encoded_.mask_identity = std::exchange(other.encoded_.mask_identity, std::nullopt);
            encoded_.psf = std::move(other.encoded_.psf);
            encoded_.psf_identity = std::exchange(other.encoded_.psf_identity, std::nullopt);
            ready_ = std::exchange(other.ready_, false);
        }
        return *this;
    }
    ~Input() = default;
    [[nodiscard]] EncodedInputs& encoded() & noexcept {
        return encoded_;
    }
    [[nodiscard]] const EncodedInputs& encoded() const& noexcept {
        return encoded_;
    }
    [[nodiscard]] EncodedInputs& encoded() && = delete;
    [[nodiscard]] const EncodedInputs& encoded() const&& = delete;
    [[nodiscard]] bool ready() const noexcept {
        const auto agrees = [](const core::Buffer& bytes, const core::ContentIdentity& identity) {
            return identity.bytes == bytes.size() &&
                   core::valid_hexadecimal(identity.sha256, core::sha256_hex_length);
        };
        return ready_ && agrees(encoded_.source, encoded_.source_identity) &&
               (encoded_.mask_identity ? agrees(encoded_.mask, *encoded_.mask_identity)
                                       : encoded_.mask.empty()) &&
               (encoded_.psf_identity ? agrees(encoded_.psf, *encoded_.psf_identity)
                                      : encoded_.psf.empty());
    }

  private:
    // Acquired empty input can be identified and ready; consumed input cannot be revived.
    EncodedInputs encoded_;
    bool ready_ = true;
};
struct PsfAsset {
    core::Buffer encoded;
    core::ContentIdentity identity;
};
struct OutputFacts {
    image::PngShape shape;
    bool profile_embedded = false;
    std::optional<image::Resolution> resolution;
    bool decoded_and_compared = false;
};
struct Rendering {
    codec::EncodedPng result;
    std::optional<codec::EncodedPng> mask;
    std::optional<PsfAsset> psf;
    image::SourceFacts source;
    image::InterpretationReport interpretation;
    methods::StageReports stages;
    OutputFacts output;
};
struct Failure {
    core::Error error;
    std::optional<methods::Family> failed_stage;
    methods::CompletedStages completed;
    methods::FailureWarningContext warning_context;
};
using RenderResult = std::expected<Rendering, Failure>;
[[nodiscard]] RenderResult render(Input input, const methods::Plan& plan, core::Budget& budget,
                                  const exec::Scheduler& scheduler);
} // namespace docenhance::pipeline
