#include "control/json_codec/codec_detail.h"

#include "owt/protocol/v5/contract.h"

#include <set>

namespace control::json_codec::detail {

bool reject_unknown_fields(
    const json& obj,
    std::initializer_list<std::string_view> allowed,
    std::string& unknown_key) {
  if (!obj.is_object()) {
    unknown_key.clear();
    return false;
  }

  const std::set<std::string_view> allowed_set(allowed.begin(), allowed.end());
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    if (allowed_set.find(it.key()) == allowed_set.end()) {
      unknown_key = it.key();
      return false;
    }
  }
  unknown_key.clear();
  return true;
}

bool is_valid_id(const json& id) {
  return id.is_null() || id.is_string() || id.is_number_integer() || id.is_number_unsigned();
}

std::string kind_for_type(message_type type) {
  switch (type) {
    case message_type::agent_register:
    case message_type::agent_heartbeat:
    case message_type::agent_command_ack:
    case message_type::agent_command_result:
      return std::string(owt::protocol::v5::kind::kAction);
    case message_type::server_register_ack:
    case message_type::server_command_dispatch:
      return std::string(owt::protocol::v5::kind::kEvent);
    case message_type::server_error:
      return std::string(owt::protocol::v5::kind::kError);
  }
  return std::string(owt::protocol::v5::kind::kError);
}

} // namespace control::json_codec::detail
