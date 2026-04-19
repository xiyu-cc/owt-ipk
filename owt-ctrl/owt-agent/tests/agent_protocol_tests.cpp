#include "control/control_json_codec.h"
#include "control/control_protocol.h"
#include "owt/protocol/v4/contract.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void test_message_type_mapping_v4() {
  control::message_type type = control::message_type::server_error;

  require(
      control::try_parse_message_type(
          std::string(owt::protocol::v4::agent::kTypeAgentRegister),
          type) &&
          type == control::message_type::agent_register,
      "agent.register parse failed");

  require(
      control::to_string(control::message_type::server_command_dispatch) ==
          owt::protocol::v4::agent::kTypeServerCommandDispatch,
      "server.command.dispatch stringify failed");
}

void test_v4_envelope_roundtrip() {
  control::envelope in;
  in.type = control::message_type::agent_command_result;
  in.protocol = std::string(owt::protocol::v4::kProtocol);
  in.sent_at_ms = 123456;
  in.trace_id = "trc-1";
  in.agent_id = "agent-1";
  in.data = control::command_result_payload{
      "cmd-1",
      control::command_status::succeeded,
      0,
      nlohmann::json{{"ok", true}}};

  const auto encoded = control::encode_envelope_json(in);

  control::envelope out;
  std::string error;
  require(control::decode_envelope_json(encoded, out, error), "decode roundtrip failed: " + error);
  require(out.type == control::message_type::agent_command_result, "decoded type mismatch");
  require(out.protocol == "v4", "decoded protocol mismatch");
  require(out.trace_id == "trc-1", "decoded trace_id mismatch");

  const auto* payload = std::get_if<control::command_result_payload>(&out.data);
  require(payload != nullptr, "decoded payload type mismatch");
  require(payload->command_id == "cmd-1", "decoded command_id mismatch");
  require(payload->final_status == control::command_status::succeeded, "decoded final status mismatch");
}

void test_server_command_dispatch_decode() {
  const std::string text = R"({
    "type":"server.command.dispatch",
    "meta":{"protocol":"v4","ts_ms":100,"trace_id":"trc-2","agent_id":"agent-2"},
    "data":{"command":{
      "command_id":"cmd-2",
      "idempotency_key":"cmd-2",
      "command_type":"host_probe_get",
      "issued_at_ms":100,
      "expires_at_ms":200,
      "timeout_ms":5000,
      "max_retry":1,
      "payload":{}
    }}
  })";

  control::envelope out;
  std::string error;
  require(control::decode_envelope_json(text, out, error), "decode dispatch failed: " + error);
  require(out.type == control::message_type::server_command_dispatch, "dispatch type mismatch");

  const auto* cmd = std::get_if<control::command>(&out.data);
  require(cmd != nullptr, "dispatch command payload missing");
  require(cmd->command_id == "cmd-2", "dispatch command_id mismatch");
  require(cmd->type == control::command_type::host_probe_get, "dispatch command_type mismatch");
}

void test_reject_legacy_fields() {
  const std::string legacy_text =
      R"({"type":"agent.heartbeat","meta":{"version":"v3","ts_ms":1,"trace_id":"t"},"payload":{}})";

  control::envelope out;
  std::string error;
  require(!control::decode_envelope_json(legacy_text, out, error), "legacy envelope should be rejected");
  require(!error.empty(), "legacy decode error should not be empty");
}

} // namespace

int main() {
  try {
    test_message_type_mapping_v4();
    test_v4_envelope_roundtrip();
    test_server_command_dispatch_decode();
    test_reject_legacy_fields();
    std::cout << "owt-agent protocol tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "owt-agent protocol tests failed: " << ex.what() << '\n';
    return 1;
  }
}
