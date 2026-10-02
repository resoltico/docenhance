// SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
// SPDX-License-Identifier: MIT
#include "docenhance/bundle/inventory.hpp"
#include "docenhance/core/result.hpp"
#include "read_fields.hpp"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace docenhance::bundle {
namespace {
// SAX false terminates parsing; a DOM discard callback does not. Only admitted event counts
// and unique keys reach DOM construction. Both passes use the same library JSON grammar.
class RecordAdmission final : public nlohmann::json_sax<RecordJson> {
  public:
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
        if (!event()) {
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
        keys_.pop_back();
        return true;
    }
    bool start_array(std::size_t /*elements*/) override {
        return event();
    }
    bool end_array() override {
        return event();
    }
    bool parse_error(std::size_t /*position*/, const std::string& /*token*/,
                     const RecordJson::exception& /*error*/) override {
        return false;
    }
    [[nodiscard]] bool excessive() const noexcept {
        return events_ > record_max_events;
    }

  private:
    bool event() noexcept {
        return ++events_ <= record_max_events;
    }
    std::size_t events_ = 0;
    std::vector<std::set<std::string>> keys_;
};
} // namespace
core::Result<RecordJson> parse_record(std::string_view text) {
    {
        RecordAdmission admission;
        if (!RecordJson::sax_parse(text, &admission)) {
            return core::failure(core::ErrorCode::input,
                                 admission.excessive()
                                     ? "The run record exceeds its parser event ceiling"
                                     : "The run record is not well-formed JSON with unique keys");
        }
    } // Release admission bookkeeping before constructing the bounded DOM.
    auto document = RecordJson::parse(text, nullptr, false);
    if (document.is_discarded()) {
        return core::failure(core::ErrorCode::input, "The run record is not well-formed JSON");
    }
    return document;
}
} // namespace docenhance::bundle
