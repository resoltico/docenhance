// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <string_view>
namespace docenhance::methods {
// Preparation and once-only application observation are independent of numerical disposition.
enum class StageProgress { not_started, preparing, prepared, evaluating, completed };
[[nodiscard]] constexpr std::string_view progress_name(StageProgress progress) noexcept {
    switch (progress) {
    case StageProgress::not_started:
        return "not_started";
    case StageProgress::preparing:
        return "preparing";
    case StageProgress::prepared:
        return "prepared";
    case StageProgress::evaluating:
        return "evaluating";
    case StageProgress::completed:
        return "completed";
    }
    return {};
}
template <class Report> [[nodiscard]] bool valid_progress(const Report& r) noexcept {
    using Status = decltype(r.status);
    if (static_cast<unsigned>(r.progress) > static_cast<unsigned>(StageProgress::completed)) {
        return false;
    }
    if (static_cast<unsigned>(r.status) > static_cast<unsigned>(Status::failed) ||
        static_cast<unsigned>(r.reason) >
            static_cast<unsigned>(decltype(r.reason)::processing_failure)) {
        return false;
    }
    if ((r.progress == StageProgress::preparing || r.progress == StageProgress::prepared) &&
        (r.evaluated_samples != 0 || r.changed_samples != 0)) {
        return false;
    }
    if (r.complete()) {
        return r.status != Status::unresolved && r.status != Status::failed;
    }
    if (r.status == Status::failed) {
        return r.requested.has_value() && r.progress != StageProgress::not_started;
    }
    if (!r.requested) {
        return r.status == Status::disabled && r.reason == decltype(r.reason)::none &&
               r.progress != StageProgress::prepared && r.progress != StageProgress::evaluating;
    }
    if (r.status != Status::unresolved) {
        return false;
    }
    return r.reason == decltype(r.reason)::none;
}
} // namespace docenhance::methods
