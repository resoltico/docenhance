// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "docenhance/contract/command.hpp"
#include "docenhance/core/cancellation.hpp"
#include "docenhance/core/identity.hpp"
#include "docenhance/core/result.hpp"
#include "docenhance/core/utf8.hpp"

#include <expected>
#include <string>
#include <utility>
#include <vector>

namespace docenhance::app {
// Reading a bundle back. The application admits the request; an adapter cannot construct one that
// was not validated, and nothing here touches a filesystem.
class VerifyRequest {
  public:
    VerifyRequest(const VerifyRequest&) = default;
    VerifyRequest& operator=(const VerifyRequest&) = default;
    VerifyRequest(VerifyRequest&& other) noexcept
        : directory_(std::exchange(other.directory_, {})) {}
    VerifyRequest& operator=(VerifyRequest&& other) noexcept {
        if (this != &other) {
            directory_ = std::exchange(other.directory_, {});
        }
        return *this;
    }
    ~VerifyRequest() = default;
    [[nodiscard]] bool ready() const noexcept {
        return core::valid_path(directory_);
    }
    [[nodiscard]] const std::string& directory() const& noexcept {
        return directory_;
    }
    [[nodiscard]] const std::string& directory() const&& = delete;

  private:
    friend core::Result<VerifyRequest> prepare_verify(const contract::Invocation& /*invocation*/);
    explicit VerifyRequest(std::string directory) : directory_(std::move(directory)) {}
    std::string directory_;
};
[[nodiscard]] core::Result<VerifyRequest> prepare_verify(const contract::Invocation& invocation);

// What a bundle agreed with. Confirming a bundle establishes that its artifacts are the ones its
// record names; it does not establish who produced them, and it never reruns the processing that
// the record describes.
struct Verified {
    std::string directory;
    std::string run;
    std::string recorded;
    std::vector<core::NamedContent> confirmed;
};

// A deliberate effect boundary, like the processing port. Verification is read-only: it opens no
// path the record names outside the bundle and executes nothing it finds.
class Verifier {
  public:
    Verifier() = default;
    Verifier(const Verifier&) = delete;
    Verifier& operator=(const Verifier&) = delete;
    Verifier(Verifier&&) = delete;
    Verifier& operator=(Verifier&&) = delete;
    virtual ~Verifier() = default;
    [[nodiscard]] virtual core::Result<Verified> verify(const VerifyRequest& request,
                                                        const core::Cancellation& cancellation) = 0;
};
} // namespace docenhance::app
