// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/image/continuous.hpp"
#include "docenhance/image/source.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/otsu.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"

#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
namespace docenhance::app {
using Operation = std::variant<methods::Binarization, image::Continuous>;
// The application admits requests; adapters cannot construct an unvalidated request.
class ProcessRequest {
  public:
    ProcessRequest(const ProcessRequest&) = default;
    ProcessRequest& operator=(const ProcessRequest&) = default;
    ProcessRequest(ProcessRequest&& other) noexcept
        : input_(std::exchange(other.input_, {})), output_(std::move(other.output_)),
          operation_(other.operation_), illumination_(other.illumination_),
          protection_(std::move(other.protection_)), denoising_(other.denoising_),
          restoration_(std::move(other.restoration_)), contrast_(other.contrast_),
          sharpening_(other.sharpening_) {}
    ProcessRequest& operator=(ProcessRequest&& other) noexcept {
        if (this != &other) {
            input_ = std::exchange(other.input_, {});
            output_ = std::move(other.output_);
            operation_ = other.operation_;
            illumination_ = other.illumination_;
            protection_ = std::move(other.protection_);
            denoising_ = other.denoising_;
            restoration_ = std::move(other.restoration_);
            contrast_ = other.contrast_;
            sharpening_ = other.sharpening_;
        }
        return *this;
    }
    ~ProcessRequest() = default;
    [[nodiscard]] bool ready() const noexcept {
        return core::valid_path(input_) && core::valid_path(output_) &&
               (!protection_ || core::valid_path(*protection_)) &&
               methods::valid_restoration_paths(restoration_);
    }
    [[nodiscard]] const std::string& input() const& noexcept {
        return input_;
    }
    [[nodiscard]] const std::string& input() const&& = delete;
    [[nodiscard]] const std::string& output_directory() const& noexcept {
        return output_;
    }
    [[nodiscard]] const std::string& output_directory() const&& = delete;
    [[nodiscard]] const methods::Illumination& illumination() const& noexcept {
        return illumination_;
    }
    [[nodiscard]] const methods::Illumination& illumination() const&& = delete;
    [[nodiscard]] const std::optional<std::string>& protection() const& noexcept {
        return protection_;
    }
    [[nodiscard]] const std::optional<std::string>& protection() const&& = delete;
    [[nodiscard]] const methods::Denoising& denoising() const& noexcept {
        return denoising_;
    }
    [[nodiscard]] const methods::Denoising& denoising() const&& = delete;
    [[nodiscard]] const methods::Restoration& restoration() const& noexcept {
        return restoration_;
    }
    [[nodiscard]] const methods::Restoration& restoration() const&& = delete;
    [[nodiscard]] const methods::Contrast& contrast() const& noexcept {
        return contrast_;
    }
    [[nodiscard]] const methods::Contrast& contrast() const&& = delete;
    [[nodiscard]] const methods::Sharpening& sharpening() const& noexcept {
        return sharpening_;
    }
    [[nodiscard]] const methods::Sharpening& sharpening() const&& = delete;
    [[nodiscard]] const Operation& operation() const& noexcept {
        return operation_;
    }
    [[nodiscard]] const Operation& operation() const&& = delete;

  private:
    friend core::Result<ProcessRequest> prepare_process(const contract::Invocation& /*invocation*/);
    struct Enhancements {
        methods::Illumination illumination;
        methods::Denoising denoising;
        methods::Restoration restoration;
        methods::Contrast contrast;
        methods::Sharpening sharpening;
    };
    ProcessRequest(std::string input, std::string output, Operation operation,
                   Enhancements enhancements, std::optional<std::string> protection)
        : input_(std::move(input)), output_(std::move(output)), operation_(operation),
          illumination_(enhancements.illumination), protection_(std::move(protection)),
          denoising_(enhancements.denoising), restoration_(std::move(enhancements.restoration)),
          contrast_(enhancements.contrast), sharpening_(enhancements.sharpening) {}
    std::string input_;
    std::string output_;
    Operation operation_;
    methods::Illumination illumination_;
    std::optional<std::string> protection_;
    methods::Denoising denoising_;
    methods::Restoration restoration_;
    methods::Contrast contrast_;
    methods::Sharpening sharpening_;
};
[[nodiscard]] core::Result<ProcessRequest> prepare_process(const contract::Invocation& invocation);
// A published bundle is identified by the run and record digest, not a persisted publication claim.
// Success alternatives carry only the observations belonging to their representation.
struct PublishedBinary {
    std::string output;
    std::string run;
    core::ContentIdentity record;
    std::optional<image::SourceDescription> source_decoding = std::nullopt;
    std::optional<methods::OtsuObservation> otsu = std::nullopt;
};
struct PublishedContinuous {
    std::string output;
    image::ConversionReport conversion;
    methods::IlluminationReport illumination;
    std::string run;
    core::ContentIdentity record;
    std::optional<image::SourceDescription> source_decoding = std::nullopt;
    methods::DenoisingReport denoising;
    methods::ContrastReport contrast;
    methods::SharpenReport sharpening;
    methods::RestorationReport restoration;
};
using Published = std::variant<PublishedBinary, PublishedContinuous>;
struct Processed {
    std::string output;
    methods::ImplementedMethod method;
    std::string run;
    core::ContentIdentity record;
    std::optional<image::SourceDescription> source_decoding = std::nullopt;
    std::optional<methods::OtsuObservation> otsu = std::nullopt;
};
struct ProcessFailure {
    core::Error error;
    std::optional<methods::DenoisingReport> denoising = std::nullopt;
    std::optional<methods::ContrastReport> contrast = std::nullopt;
    std::optional<methods::SharpenReport> sharpening = std::nullopt;
    std::optional<methods::IlluminationReport> illumination = std::nullopt;
    std::optional<methods::RestorationReport> restoration = std::nullopt;
};
using ProcessResult = std::expected<Published, ProcessFailure>;
[[nodiscard]] inline std::unexpected<ProcessFailure>
process_failure(core::Error error, std::optional<methods::IlluminationReport> illumination = {},
                std::optional<methods::DenoisingReport> denoising = {},
                std::optional<methods::ContrastReport> contrast = {},
                std::optional<methods::SharpenReport> sharpening = {}) {
    return std::unexpected(ProcessFailure{
        .error = std::move(error),
        .denoising = denoising,
        .contrast = contrast,
        .sharpening = sharpening,
        .illumination = illumination,
    });
}
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
