// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MPL-2.0
#include "docenhance/record/json.hpp"

#include "docenhance/core/result.hpp"
#include "docenhance/record/codec.hpp"
#include "docenhance/record/record.hpp"

#include <cstddef>
#include <expected>
#include <new>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace docenhance::record {
namespace {
// SAX refusal stops parsing before the bounded DOM is constructed.
class JsonAdmission final : public nlohmann::json_sax<Json> {
  public:
    explicit JsonAdmission(JsonLimits limits) noexcept : limits_(limits) {}
    bool null() override {
        return event();
    }
    bool boolean(bool /*value*/) override {
        return event();
    }
    bool number_integer(number_integer_t /*value*/) override {
        return event();
    }
    bool number_unsigned(number_unsigned_t /*value*/) override {
        return event();
    }
    bool number_float(number_float_t /*value*/, const string_t& /*text*/) override {
        return event();
    }
    bool string(string_t& /*value*/) override {
        return event();
    }
    bool binary(binary_t& /*value*/) override {
        return false;
    }
    bool start_object(std::size_t /*elements*/) override {
        if (!enter()) {
            return false;
        }
        keys_.emplace_back();
        return true;
    }
    bool key(string_t& value) override {
        return event() && !keys_.empty() && keys_.back().insert(value).second;
    }
    bool end_object() override {
        if (!event() || keys_.empty()) {
            return false;
        }
        --depth_;
        keys_.pop_back();
        return true;
    }
    bool start_array(std::size_t /*elements*/) override {
        return enter();
    }
    bool end_array() override {
        --depth_;
        return event();
    }
    bool parse_error(std::size_t /*position*/, const std::string& /*token*/,
                     const Json::exception& /*error*/) override {
        return false;
    }
    [[nodiscard]] bool too_deep() const noexcept {
        return depth_ > limits_.depth;
    }
    [[nodiscard]] bool excessive() const noexcept {
        return events_ > limits_.events;
    }

  private:
    bool enter() noexcept {
        return event() && ++depth_ <= limits_.depth;
    }
    bool event() noexcept {
        return ++events_ <= limits_.events;
    }
    JsonLimits limits_;
    std::size_t events_ = 0;
    std::size_t depth_ = 0;
    std::vector<std::set<std::string>> keys_;
};
} // namespace
core::Result<void> admit_json(std::string_view text, JsonLimits limits) {
    if (text.size() > limits.bytes) {
        return core::failure(core::ErrorCode::input, "The JSON document exceeds its byte ceiling");
    }
    JsonAdmission admission{limits};
    if (Json::sax_parse(text, &admission)) {
        return {};
    }
    if (admission.too_deep()) {
        return core::failure(core::ErrorCode::input,
                             "The JSON document is nested beyond its depth ceiling");
    }
    return core::failure(core::ErrorCode::input,
                         admission.excessive()
                             ? "The JSON document exceeds its parser event ceiling"
                             : "The JSON document is not well-formed JSON with unique keys");
}
core::Result<std::string> serialize(const RunRecord& value) {
    try {
        auto encoded = Codec::encode(value);
        if (!encoded) {
            return std::unexpected(std::move(encoded.error()));
        }
        auto bytes = encoded->dump(2, ' ', false, Json::error_handler_t::strict) + "\n";
        auto admitted = admit_json(
            bytes,
            {.bytes = record_max_bytes, .depth = record_max_depth, .events = record_max_events});
        if (!admitted) {
            if (admitted.error().code == core::ErrorCode::resource) {
                return std::unexpected(std::move(admitted.error()));
            }
            return core::failure(core::ErrorCode::invariant,
                                 "The run record exceeds its closed metadata domain");
        }
        return bytes;
    } catch (const std::bad_alloc&) {
        return core::failure(core::ErrorCode::resource, "Writing the run record exhausted memory");
    } catch (const Json::exception&) {
        return core::failure(core::ErrorCode::invariant,
                             "The run record contains malformed identity text");
    }
}
} // namespace docenhance::record
