// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/invocation.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/otsu.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/stages.hpp"

#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
namespace docenhance::app {
struct BinaryPlan {
    methods::Binarization operation;
    image::QuarterTurn rotation = image::QuarterTurn::identity();
};
struct PhotometricPlan {
    methods::Illumination illumination;
    methods::Denoising denoising;
    methods::Restoration restoration;
    methods::Contrast contrast;
    methods::Sharpening sharpening;
};
struct ContinuousPlan {
    image::Continuous operation;
    PhotometricPlan photometric;
    std::optional<std::string> protection;
    image::QuarterTurn rotation = image::QuarterTurn::identity();
};
using ProcessPlan = std::variant<BinaryPlan, ContinuousPlan>;
// Only admission constructs requests. Binary plans cannot contain continuous stage or mask state.
class ProcessRequest {
  public:
    ProcessRequest(const ProcessRequest&) = default;
    ProcessRequest& operator=(const ProcessRequest&) = default;
    ProcessRequest(ProcessRequest&& other) noexcept
        : input_(std::exchange(other.input_, {})), output_(std::move(other.output_)),
          plan_(std::move(other.plan_)) {}
    ProcessRequest& operator=(ProcessRequest&& other) noexcept {
        if (this != &other) {
            input_ = std::exchange(other.input_, {});
            output_ = std::move(other.output_);
            plan_ = std::move(other.plan_);
        }
        return *this;
    }
    ~ProcessRequest() = default;
    [[nodiscard]] bool ready() const noexcept {
        if (plan_.valueless_by_exception() || !core::valid_path(input_) ||
            !core::valid_path(output_)) {
            return false;
        }
        const auto* const continuous = std::get_if<ContinuousPlan>(&plan_);
        return continuous == nullptr ||
               ((!continuous->protection || core::valid_path(*continuous->protection)) &&
                methods::valid_restoration_paths(continuous->photometric.restoration));
    }
    [[nodiscard]] const std::string& input() const& noexcept {
        return input_;
    }
    [[nodiscard]] const std::string& input() const&& = delete;
    [[nodiscard]] const std::string& output_directory() const& noexcept {
        return output_;
    }
    [[nodiscard]] const std::string& output_directory() const&& = delete;
    [[nodiscard]] const ProcessPlan& plan() const& noexcept {
        return plan_;
    }
    [[nodiscard]] const ProcessPlan& plan() const&& = delete;
    [[nodiscard]] image::QuarterTurn rotation() const {
        return std::visit([](const auto& plan) { return plan.rotation; }, plan_);
    }

  private:
    friend core::Result<ProcessRequest> prepare_process(const app::Invocation& /*invocation*/);
    ProcessRequest(std::string input, std::string output, ProcessPlan plan)
        : input_(std::move(input)), output_(std::move(output)), plan_(std::move(plan)) {}
    std::string input_;
    std::string output_;
    ProcessPlan plan_;
};
[[nodiscard]] core::Result<ProcessRequest> prepare_process(const app::Invocation& invocation);
// A published bundle is identified by the run and record digest, not a persisted publication claim.
// Success alternatives carry only the observations belonging to their representation.
struct PublishedBinary {
    std::string output;
    std::string run;
    core::ContentIdentity record;
    std::optional<image::SourceFacts> source_decoding = std::nullopt;
    std::optional<methods::OtsuObservation> otsu = std::nullopt;
    image::QuarterTurn rotation = image::QuarterTurn::identity();
};
struct PublishedContinuous {
    std::string output;
    image::AlphaPolicy alpha = image::AlphaPolicy::white;
    image::ConversionReport conversion;
    methods::StageObservations stages;
    std::string run;
    core::ContentIdentity record;
    std::optional<image::SourceFacts> source_decoding = std::nullopt;
};
using Published = std::variant<PublishedBinary, PublishedContinuous>;
struct Processed {
    std::string output;
    methods::ImplementedMethod method;
    std::string run;
    core::ContentIdentity record;
    std::optional<image::SourceFacts> source_decoding = std::nullopt;
    std::optional<methods::OtsuObservation> otsu = std::nullopt;
    image::QuarterTurn rotation = image::QuarterTurn::identity();
};
struct ProcessFailure {
    core::Error error;
    std::optional<methods::StageObservations> stages = std::nullopt;
};
using ProcessResult = std::expected<Published, ProcessFailure>;
[[nodiscard]] inline std::unexpected<ProcessFailure>
process_failure(core::Error error, std::optional<methods::StageObservations> stages = {}) {
    return std::unexpected(ProcessFailure{.error = std::move(error), .stages = std::move(stages)});
}
[[nodiscard]] methods::StageObservations initial_stages(const PhotometricPlan& plan);
// A deliberate effect boundary. There is no default implementation or hidden service lookup.
// Expected errors retain their publication state in Result. If a port throws, the application
// cannot prove whether its effects committed and returns publication_unknown, never not_started.
class Processor {
  public:
    Processor() = default;
    Processor(const Processor&) = delete;
    Processor& operator=(const Processor&) = delete;
    Processor(Processor&&) = delete;
    Processor& operator=(Processor&&) = delete;
    virtual ~Processor() = default;
    [[nodiscard]] virtual ProcessResult process(const ProcessRequest& request,
                                                const core::Cancellation& cancellation) = 0;
};
} // namespace docenhance::app
