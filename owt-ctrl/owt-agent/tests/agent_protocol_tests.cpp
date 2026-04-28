#include "control/control_json_codec.h"
#include "control/control_protocol.h"
#include "owt/protocol/v5/contract.h"

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

void test_message_type_mapping_v5() {
  control::message_type type = control::message_type::server_error;

  require(
      control::try_parse_message_type(
          std::string(owt::protocol::v5::agent::kActionAgentRegister),
          type) &&
          type == control::message_type::agent_register,
      "agent.register parse failed");

  require(
      control::to_string(control::message_type::server_command_dispatch) ==
          owt::protocol::v5::agent::kEventCommandDispatch,
      "command.dispatch stringify failed");
}

void test_v5_envelope_roundtrip() {
  control::envelope in;
  in.type = control::message_type::agent_command_result;
  in.version = std::string(owt::protocol::v5::kProtocol);
  in.id = "req-1";
  in.ts_ms = 123456;
  in.target = "AA:00:00:00:10:01";
  in.payload = control::command_result_payload{
      "AA:00:00:00:10:01",
      "agent-10-01",
      "cmd-1",
      control::command_status::succeeded,
      0,
      nlohmann::json{{"ok", true}}};

  const auto encoded = control::encode_envelope_json(in);

  control::envelope out;
  std::string error;
  require(control::decode_envelope_json(encoded, out, error), "decode roundtrip failed: " + error);
  require(out.type == control::message_type::agent_command_result, "decoded type mismatch");
  require(out.version == "v5", "decoded version mismatch");
  require(out.id.is_string() && out.id.get<std::string>() == "req-1", "decoded id mismatch");
  require(out.target == "AA:00:00:00:10:01", "decoded target mismatch");

  const auto* payload = std::get_if<control::command_result_payload>(&out.payload);
  require(payload != nullptr, "decoded payload type mismatch");
  require(payload->agent_mac == "AA:00:00:00:10:01", "decoded agent_mac mismatch");
  require(payload->agent_id == "agent-10-01", "decoded agent_id mismatch");
  require(payload->command_id == "cmd-1", "decoded command_id mismatch");
  require(payload->final_status == control::command_status::succeeded, "decoded final status mismatch");
}

void test_server_command_dispatch_decode() {
  const std::string text = R"({
    "v":"v5",
    "kind":"event",
    "name":"command.dispatch",
    "id":"req-2",
    "ts_ms":100,
    "target":"AA:00:00:00:20:01",
    "payload":{"command":{
      "command_id":"cmd-2",
      "idempotency_key":"cmd-2",
      "command_type":"host_reboot",
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
  require(out.id.is_string() && out.id.get<std::string>() == "req-2", "dispatch id mismatch");

  const auto* cmd = std::get_if<control::command>(&out.payload);
  require(cmd != nullptr, "dispatch command payload missing");
  require(cmd->command_id == "cmd-2", "dispatch command_id mismatch");
  require(cmd->type == control::command_type::host_reboot, "dispatch command_type mismatch");
}

void test_reject_kind_mismatch() {
  const std::string bad_kind = R"({
    "v":"v5",
    "kind":"action",
    "name":"command.dispatch",
    "id":"req-3",
    "ts_ms":1,
    "payload":{"command":{
      "command_id":"cmd-x",
      "idempotency_key":"cmd-x",
      "command_type":"host_reboot",
      "issued_at_ms":1,
      "expires_at_ms":2,
      "timeout_ms":1000,
      "max_retry":1,
      "payload":{}
    }}
  })";

  control::envelope out;
  std::string error;
  require(!control::decode_envelope_json(bad_kind, out, error), "kind mismatch should be rejected");
  require(!error.empty(), "kind mismatch decode error should not be empty");
}

void test_reject_missing_agent_mac_in_agent_actions() {
  const std::string heartbeat_missing_mac = R"({
    "v":"v5",
    "kind":"action",
    "name":"agent.heartbeat",
    "id":"req-hb",
    "ts_ms":1,
    "target":"AA:00:00:00:11:01",
    "payload":{"heartbeat_at_ms":1,"stats":{"cpu":1}}
  })";
  const std::string ack_missing_mac = R"({
    "v":"v5",
    "kind":"action",
    "name":"command.ack",
    "id":"req-ack",
    "ts_ms":2,
    "target":"AA:00:00:00:11:01",
    "payload":{"command_id":"cmd-ack","status":"acked","message":"accepted"}
  })";
  const std::string result_missing_mac = R"({
    "v":"v5",
    "kind":"action",
    "name":"command.result",
    "id":"req-result",
    "ts_ms":3,
    "target":"AA:00:00:00:11:01",
    "payload":{"command_id":"cmd-result","final_status":"succeeded","exit_code":0,"result":{"ok":true}}
  })";

  control::envelope out;
  std::string error;
  require(
      !control::decode_envelope_json(heartbeat_missing_mac, out, error),
      "heartbeat without agent_mac should be rejected");
  require(
      error == "heartbeat.agent_mac is required",
      "heartbeat missing agent_mac error mismatch");

  require(
      !control::decode_envelope_json(ack_missing_mac, out, error),
      "command.ack without agent_mac should be rejected");
  require(
      error == "command_ack.agent_mac/command_id/status are required",
      "command.ack missing agent_mac error mismatch");

  require(
      !control::decode_envelope_json(result_missing_mac, out, error),
      "command.result without agent_mac should be rejected");
  require(
      error == "command_result.agent_mac/command_id/final_status/exit_code are required",
      "command.result missing agent_mac error mismatch");
}

} // namespace

int main() {
  try {
    test_message_type_mapping_v5();
    test_v5_envelope_roundtrip();
    test_server_command_dispatch_decode();
    test_reject_kind_mismatch();
    test_reject_missing_agent_mac_in_agent_actions();
    std::cout << "owt-agent protocol tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "owt-agent protocol tests failed: " << ex.what() << '\n';
    return 1;
  }
}
