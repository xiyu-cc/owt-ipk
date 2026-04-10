#include "service/control_ws_session_observer.h"

#include "control/control_json_codec.h"
#include "log.h"
#include "server/websocket_session.h"
#include "server/websocket_session_observer.h"
#include "service/command_store.h"
#include "service/control_hub.h"
#include "service/frontend_status_stream.h"
#include "service/observability.h"
#include "service/sensitive_json.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>

namespace service {

namespace {

nlohmann::json make_default_params_payload() {
  return {
      {"wol",
       {
           {"mac", "AA:BB:CC:DD:EE:FF"},
           {"broadcast", "192.168.1.255"},
           {"port", 9},
       }},
      {"ssh",
       {
           {"host", "192.168.1.10"},
           {"port", 22},
           {"user", "root"},
           {"password", "password"},
           {"timeout_ms", 5000},
       }},
  };
}

void sync_params_to_agent(const std::string& agent_id) {
  if (agent_id.empty()) {
    return;
  }

  std::string params_json;
  std::string error;
  service::agent_params_record row;
  if (service::get_agent_params(agent_id, row, error)) {
    auto parsed = nlohmann::json::parse(row.params_json, nullptr, false);
    if (parsed.is_object()) {
      params_json = parsed.dump();
    }
  } else if (error != "agent params not found") {
    log::warn("load agent params failed during register: agent_id={}, err={}", agent_id, error);
    return;
  }

  if (params_json.empty()) {
    params_json = make_default_params_payload().dump();
    error.clear();
    if (!service::upsert_agent_params(agent_id, params_json, control::unix_time_ms_now(), error)) {
      log::warn("init agent params failed during register: agent_id={}, err={}", agent_id, error);
      return;
    }
  }

  const auto now = control::unix_time_ms_now();
  const auto sync_id = control::make_message_id();
  control::command sync_cmd;
  sync_cmd.command_id = "cmd-sync-params-" + sync_id;
  sync_cmd.idempotency_key = "idem-sync-params-" + sync_id;
  sync_cmd.type = control::command_type::params_set;
  sync_cmd.issued_at_ms = now;
  sync_cmd.expires_at_ms = now + 300000;
  sync_cmd.timeout_ms = 5000;
  sync_cmd.max_retry = 1;
  sync_cmd.payload_json = std::move(params_json);

  error.clear();
  if (!service::push_command_to_agent(agent_id, sync_cmd, error)) {
    log::warn("push initial params failed: agent_id={}, err={}", agent_id, error);
  }
}

class control_ws_session_observer final : public server::websocket_session_observer {
public:
  void on_text_message(
      const std::shared_ptr<server::websocket_session>& session,
      const std::string& text) override {
    control::envelope message;
    std::string error;
    if (!control::decode_envelope_json(text, message, error)) {
      log::warn("websocket decode message failed: {}", error);
      return;
    }
    if (!ensure_supported_protocol_version(session, message)) {
      return;
    }

    switch (message.type) {
      case control::message_type::register_agent: {
        const auto* payload = std::get_if<control::register_payload>(&message.payload);
        if (payload == nullptr || payload->agent_id.empty()) {
          log::warn("websocket register payload missing agent_id");
          return;
        }
        if (!agent_id_.empty() && agent_id_ != payload->agent_id) {
          service::unregister_control_session(agent_id_, session.get());
        }
        agent_id_ = payload->agent_id;
        service::register_control_session(agent_id_, session);
        const auto now = control::unix_time_ms_now();
        service::update_agent_register_state(*payload, now);
        sync_params_to_agent(agent_id_);
        service::broadcast_frontend_status_snapshot("agent_register", agent_id_);

        control::envelope ack;
        ack.message_id = control::make_message_id();
        ack.type = control::message_type::register_ack;
        ack.protocol_version = message.protocol_version;
        ack.sent_at_ms = now;
        ack.trace_id = message.trace_id;
        ack.agent_id = agent_id_;
        ack.payload = control::register_ack_payload{true, "registered"};
        send_control_message(session, ack);
        break;
      }

      case control::message_type::heartbeat: {
        const auto* payload = std::get_if<control::heartbeat_payload>(&message.payload);
        if (!message.agent_id.empty() && message.agent_id != agent_id_) {
          agent_id_ = message.agent_id;
          service::register_control_session(agent_id_, session);
        }
        const auto now = control::unix_time_ms_now();
        const auto heartbeat_agent_id = !message.agent_id.empty() ? message.agent_id : agent_id_;
        service::update_agent_heartbeat_state(heartbeat_agent_id, payload, now);
        service::broadcast_frontend_status_snapshot("agent_heartbeat", heartbeat_agent_id);
        control::envelope ack;
        ack.message_id = control::make_message_id();
        ack.type = control::message_type::heartbeat_ack;
        ack.protocol_version = message.protocol_version;
        ack.sent_at_ms = now;
        ack.trace_id = message.trace_id;
        ack.agent_id = agent_id_;
        ack.payload = control::heartbeat_ack_payload{ack.sent_at_ms};
        send_control_message(session, ack);
        break;
      }

      case control::message_type::command_ack: {
        const auto* payload = std::get_if<control::command_ack_payload>(&message.payload);
        if (payload == nullptr || payload->command_id.empty()) {
          return;
        }
        std::string db_error;
        const auto now = control::unix_time_ms_now();
        bool should_update_ack = true;
        service::command_record existing;
        if (service::get_command(payload->command_id, existing, db_error) &&
            is_terminal_command_status(existing.status)) {
          should_update_ack = false;
        }
        if (should_update_ack &&
            !service::update_command_status(
                payload->command_id,
                control::to_string(payload->status),
                "",
                now,
                db_error)) {
          log::warn(
              "persist ack status failed: command_id={}, err={}", payload->command_id, db_error);
        }
        nlohmann::json detail = {
            {"event", "COMMAND_ACK"},
            {"agent_id", !agent_id_.empty() ? agent_id_ : message.agent_id},
            {"message", payload->message},
        };
        db_error.clear();
        if (!service::append_command_event(
                payload->command_id,
                "COMMAND_ACK_RECEIVED",
                control::to_string(payload->status),
                detail.dump(),
                now,
                db_error)) {
          log::warn("persist ack event failed: command_id={}, err={}", payload->command_id, db_error);
        }
        break;
      }

      case control::message_type::command_result: {
        const auto* payload = std::get_if<control::command_result_payload>(&message.payload);
        if (payload == nullptr || payload->command_id.empty()) {
          return;
        }
        std::string db_error;
        const auto now = control::unix_time_ms_now();
        const auto redacted_result_json = service::redact_sensitive_json_text(payload->result_json);
        bool result_applied = false;
        if (!service::update_command_terminal_status_once(
                payload->command_id,
                control::to_string(payload->final_status),
                redacted_result_json,
                now,
                result_applied,
                db_error)) {
          log::warn("persist result status failed: command_id={}, err={}", payload->command_id, db_error);
        }
        std::string event_type = "COMMAND_RESULT_RECEIVED";
        std::string event_status = control::to_string(payload->final_status);
        nlohmann::json detail = {
            {"event", "COMMAND_RESULT"},
            {"agent_id", !agent_id_.empty() ? agent_id_ : message.agent_id},
            {"exit_code", payload->exit_code},
        };
        if (!result_applied) {
          event_type = "COMMAND_RESULT_DUPLICATE";
          detail["error_code"] = "DUPLICATE_COMMAND_RESULT";
          detail["reported_final_status"] = control::to_string(payload->final_status);
          service::command_record existing;
          std::string lookup_error;
          if (service::get_command(payload->command_id, existing, lookup_error)) {
            event_status = existing.status;
            detail["stored_final_status"] = existing.status;
          }
        }
        db_error.clear();
        if (!service::append_command_event(
                payload->command_id,
                event_type,
                event_status,
                detail.dump(),
                now,
                db_error)) {
          log::warn(
              "persist result event failed: command_id={}, err={}", payload->command_id, db_error);
        }
        if (result_applied) {
          service::record_command_terminal_status(payload->command_id, event_status, detail.dump());
        }
        service::broadcast_frontend_status_snapshot(
            result_applied ? "command_result" : "command_result_duplicate",
            !agent_id_.empty() ? agent_id_ : message.agent_id);
        break;
      }

      default:
        break;
    }
  }

  void on_session_closed(const std::shared_ptr<server::websocket_session>& session) override {
    if (!agent_id_.empty()) {
      service::unregister_control_session(agent_id_, session.get());
      service::broadcast_frontend_status_snapshot("agent_disconnected", agent_id_);
      agent_id_.clear();
    }
  }

private:
  bool ensure_supported_protocol_version(
      const std::shared_ptr<server::websocket_session>& session,
      const control::envelope& message) {
    if (control::is_supported_protocol_version(message.protocol_version)) {
      return true;
    }

    log::warn(
        "websocket protocol version rejected: agent_id={}, peer_version={}, local_version={}",
        !message.agent_id.empty() ? message.agent_id : agent_id_,
        message.protocol_version,
        control::current_protocol_version());

    control::envelope reply;
    reply.message_id = control::make_message_id();
    reply.type = control::message_type::error;
    reply.protocol_version = control::current_protocol_version();
    reply.sent_at_ms = control::unix_time_ms_now();
    reply.trace_id = !message.trace_id.empty() ? message.trace_id : reply.message_id;
    reply.agent_id = !message.agent_id.empty() ? message.agent_id : agent_id_;
    reply.payload = control::error_payload{
        "UNSUPPORTED_PROTOCOL_VERSION",
        "unsupported protocol version",
        "server=" + std::string(control::current_protocol_version()) +
            ", peer=" + message.protocol_version,
    };
    send_control_message(session, reply);
    session->close_with_reason("unsupported protocol version");
    return false;
  }

  static void send_control_message(
      const std::shared_ptr<server::websocket_session>& session,
      const control::envelope& message) {
    if (!session) {
      return;
    }
    session->send_text(control::encode_envelope_json(message));
  }

  static bool is_terminal_command_status(const std::string& status) {
    return status == "SUCCEEDED" || status == "FAILED" || status == "TIMED_OUT" ||
           status == "CANCELLED";
  }

  std::string agent_id_;
};

} // namespace

std::shared_ptr<server::websocket_session_observer> create_control_ws_session_observer() {
  return std::make_shared<control_ws_session_observer>();
}

} // namespace service
