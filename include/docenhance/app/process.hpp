// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/methods/binarization.hpp"
#include "docenhance/methods/catalog.hpp"

#include <string>
#include <utility>
namespace docenhance::app {
// The application admits requests; adapters cannot construct an unvalidated request.
class ProcessRequest {
  public:
    [[nodiscard]] const std::string& input() const noexcept {
        return input_;
    }
    [[nodiscard]] const std::string& output_directory() const noexcept {
        return output_;
    }
    [[nodiscard]] const methods::Binarization& method() const noexcept {
        return method_;
    }

  private:
    friend core::Result<ProcessRequest> prepare_process(const contract::Invocation& /*invocation*/);
    ProcessRequest(std::string input, std::string output, methods::Binarization method)
        : input_(std::move(input)), output_(std::move(output)), method_(method) {}
    std::string input_;
    std::string output_;
    methods::Binarization method_;
};
[[nodiscard]] core::Result<ProcessRequest> prepare_process(const contract::Invocation& invocation);
struct PublishedImage {
    std::string output;
};
struct Processed {
    std::string output;
    methods::ImplementedMethod method;
};
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
    [[nodiscard]] virtual core::Result<PublishedImage>
    process(const ProcessRequest& request, const core::Cancellation& cancellation) = 0;
};
} // namespace docenhance::app
