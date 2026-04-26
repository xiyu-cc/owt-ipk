#pragma once

#include "control/control_protocol.h"

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>
#include <string_view>

namespace control::json_codec::detail {

using json = nlohmann::json;

bool reject_unknown_fields(
    const json& obj,
    std::initializer_list<std::string_view> allowed,
    std::string& unknown_key);

bool is_valid_id(const json& id);
std::string kind_for_type(message_type type);

json payload_to_json(message_type type, const payload_variant& payload);
payload_variant payload_from_json(message_type type, const json& payload, bool& ok, std::string& error);
json envelope_to_json(const envelope& value);
bool envelope_from_json(const json& root, envelope& out, std::string& error);

} // namespace control::json_codec::detail
