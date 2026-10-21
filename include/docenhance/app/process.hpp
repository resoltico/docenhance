// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/app/execution_control.hpp"
#include "docenhance/app/invocation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"
#include "docenhance/image/geometry.hpp"
#include "docenhance/image/source_facts.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/catalog.hpp"
#include "docenhance/methods/contrast.hpp"
#include "docenhance/methods/denoising.hpp"
#include "docenhance/methods/illumination.hpp"
#include "docenhance/methods/plan.hpp"
#include "docenhance/methods/restoration.hpp"
#include "docenhance/methods/sharpening.hpp"
#include "docenhance/methods/stage_reports.hpp"
#include "docenhance/methods/warnings.hpp"
#include "docenhance/record/record.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <variant>
namespace docenhance::app {
// Only application admission constructs an owned request for the common fixed plan.
class ProcessRequest {
  public:
    ProcessRequest(const ProcessRequest&) = default;
    ProcessRequest& operator=(const ProcessRequest&) = default;
    ProcessRequest(ProcessRequest&& other) noexcept
        : input_(std::exchange(other.input_, {})), output_(std::exchange(other.output_, {})),
          mask_path_(std::exchange(other.mask_path_, std::nullopt)),
          psf_path_(std::exchange(other.psf_path_, std::nullopt)), plan_(other.plan_) {}
    ProcessRequest& operator=(ProcessRequest&& other) noexcept {
        if (this != &other) {
            input_ = std::exchange(other.input_, {});
            output_ = std::exchange(other.output_, {});
            mask_path_ = std::exchange(other.mask_path_, std::nullopt);
            psf_path_ = std::exchange(other.psf_path_, std::nullopt);
            plan_ = other.plan_;
        }
        return *this;
    }
    ~ProcessRequest() = default;
    [[nodiscard]] bool ready() const noexcept {
        if (!methods::validate(plan_) || !core::valid_path(input_) || !core::valid_path(output_) ||
            plan_.protection != mask_path_.has_value() ||
            (mask_path_ && !core::valid_path(*mask_path_)) ||
            (psf_path_ && !core::valid_path(*psf_path_))) {
            return false;
        }
        const auto* const wiener = std::get_if<methods::Wiener>(&plan_.photometric.restoration);
        const bool kernel =
            wiener != nullptr && std::holds_alternative<methods::Kernel>(wiener->parameters().psf);
        return kernel == psf_path_.has_value();
    }
    [[nodiscard]] const std::string& input() const& noexcept {
        return input_;
    }
    [[nodiscard]] const std::string& input() const&& = delete;
    [[nodiscard]] const std::string& output_directory() const& noexcept {
        return output_;
    }
    [[nodiscard]] const std::string& output_directory() const&& = delete;
    [[nodiscard]] const methods::Plan& plan() const& noexcept {
        return plan_;
    }
    [[nodiscard]] const methods::Plan& plan() const&& = delete;
    [[nodiscard]] const std::optional<std::string>& mask_path() const& noexcept {
        return mask_path_;
    }
    [[nodiscard]] const std::optional<std::string>& mask_path() const&& = delete;
    [[nodiscard]] const std::optional<std::string>& psf_path() const& noexcept {
        return psf_path_;
    }
    [[nodiscard]] const std::optional<std::string>& psf_path() const&& = delete;

  private:
    friend core::Result<ProcessRequest> prepare_process(const app::Invocation& /*invocation*/);
    ProcessRequest(std::string input, std::string output, std::optional<std::string> mask,
                   std::optional<std::string> psf, methods::Plan plan)
        : input_(std::move(input)), output_(std::move(output)), mask_path_(std::move(mask)),
          psf_path_(std::move(psf)), plan_(plan) {}
    std::string input_;
    std::string output_;
    std::optional<std::string> mask_path_;
    std::optional<std::string> psf_path_;
    methods::Plan plan_;
};
[[nodiscard]] core::Result<ProcessRequest> prepare_process(const app::Invocation& invocation);
// Success owns the same value serialized to run.json. Publication is confirmed by this alternative.
struct Diagnostics {
    std::uint32_t threads{};
    std::uint64_t memory_limit_mib{};
    std::uint64_t charged_peak_bytes{};
    bool operator==(const Diagnostics&) const = default;
};
struct Published {
    std::string output;
    core::ContentIdentity record_identity;
    record::RunRecord record;
    Diagnostics diagnostics;
};
struct ProcessFailure {
    core::Error error;
    core::Publication publication = core::Publication::not_started;
    std::optional<methods::Family> failed_stage = std::nullopt;
    methods::CompletedStages completed{};
    methods::FailureWarningContext warning_context{};
    // Null until the executor has resolved real settings; never infer work from defaults.
    std::optional<Diagnostics> diagnostics = std::nullopt;
};
using ProcessResult = std::expected<Published, ProcessFailure>;
[[nodiscard]] inline std::unexpected<ProcessFailure>
process_failure(core::Error error, core::Publication publication) {
    return std::unexpected(ProcessFailure{.error = std::move(error), .publication = publication});
}
// A deliberate effect boundary. There is no default implementation or hidden service lookup.
// Expected errors carry an explicit publication state. If a port throws, the application
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
                                                const ExecutionControl& control) = 0;
};
} // namespace docenhance::app
