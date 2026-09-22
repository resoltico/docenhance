// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#pragma once
#include "docenhance/contract/command.hpp"
#include "docenhance/core/result.hpp"

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
    [[nodiscard]] double threshold() const noexcept {
        return threshold_;
    }

  private:
    friend core::Result<ProcessRequest> prepare_process(const contract::Invocation& /*invocation*/);
    ProcessRequest(std::string input, std::string output, double threshold)
        : input_(std::move(input)), output_(std::move(output)), threshold_(threshold) {}
    std::string input_;
    std::string output_;
    double threshold_;
};
[[nodiscard]] core::Result<ProcessRequest> prepare_process(const contract::Invocation& invocation);
struct Processed {
    std::string output;
};
// A deliberate effect boundary. There is no default implementation or hidden service lookup.
class Processor {
  public:
    Processor() = default;
    Processor(const Processor&) = delete;
    Processor& operator=(const Processor&) = delete;
    Processor(Processor&&) = delete;
    Processor& operator=(Processor&&) = delete;
    virtual ~Processor() = default;
    [[nodiscard]] virtual core::Result<Processed> process(const ProcessRequest& request) = 0;
};
} // namespace docenhance::app
