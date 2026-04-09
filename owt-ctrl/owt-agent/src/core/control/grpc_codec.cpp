#include "control/grpc_codec.h"

#if OWT_CTRL_ENABLE_GRPC_TRANSPORT

namespace control {

namespace pb = owt::control::v1;

namespace {

bool encode_channel_type(channel_type in, pb::ChannelType& out) {
  switch (in) {
    case channel_type::wss:
      out = pb::CHANNEL_TYPE_WSS;
      return true;
    case channel_type::grpc:
      out = pb::CHANNEL_TYPE_GRPC;
      return true;
  }
  return false;
}

bool decode_channel_type(pb::ChannelType in, channel_type& out) {
  switch (in) {
    case pb::CHANNEL_TYPE_WSS:
      out = channel_type::wss;
      return true;
    case pb::CHANNEL_TYPE_GRPC:
      out = channel_type::grpc;
      return true;
    default:
      return false;
  }
}

bool encode_message_type(message_type in, pb::MessageType& out) {
  switch (in) {
    case message_type::register_agent:
      out = pb::MESSAGE_TYPE_REGISTER;
      return true;
    case message_type::register_ack:
      out = pb::MESSAGE_TYPE_REGISTER_ACK;
      return true;
    case message_type::heartbeat:
      out = pb::MESSAGE_TYPE_HEARTBEAT;
      return true;
    case message_type::heartbeat_ack:
      out = pb::MESSAGE_TYPE_HEARTBEAT_ACK;
      return true;
    case message_type::command_push:
      out = pb::MESSAGE_TYPE_COMMAND_PUSH;
      return true;
    case message_type::command_ack:
      out = pb::MESSAGE_TYPE_COMMAND_ACK;
      return true;
    case message_type::command_result:
      out = pb::MESSAGE_TYPE_COMMAND_RESULT;
      return true;
    case message_type::error:
      out = pb::MESSAGE_TYPE_ERROR;
      return true;
  }
  return false;
}

bool decode_message_type(pb::MessageType in, message_type& out) {
  switch (in) {
    case pb::MESSAGE_TYPE_REGISTER:
      out = message_type::register_agent;
      return true;
    case pb::MESSAGE_TYPE_REGISTER_ACK:
      out = message_type::register_ack;
      return true;
    case pb::MESSAGE_TYPE_HEARTBEAT:
      out = message_type::heartbeat;
      return true;
    case pb::MESSAGE_TYPE_HEARTBEAT_ACK:
      out = message_type::heartbeat_ack;
      return true;
    case pb::MESSAGE_TYPE_COMMAND_PUSH:
      out = message_type::command_push;
      return true;
    case pb::MESSAGE_TYPE_COMMAND_ACK:
      out = message_type::command_ack;
      return true;
    case pb::MESSAGE_TYPE_COMMAND_RESULT:
      out = message_type::command_result;
      return true;
    case pb::MESSAGE_TYPE_ERROR:
      out = message_type::error;
      return true;
    default:
      return false;
  }
}

bool encode_command_type(command_type in, pb::CommandType& out) {
  switch (in) {
    case command_type::wol_wake:
      out = pb::COMMAND_TYPE_WOL_WAKE;
      return true;
    case command_type::host_reboot:
      out = pb::COMMAND_TYPE_HOST_REBOOT;
      return true;
    case command_type::host_poweroff:
      out = pb::COMMAND_TYPE_HOST_POWEROFF;
      return true;
    case command_type::host_probe_get:
      out = pb::COMMAND_TYPE_HOST_PROBE_GET;
      return true;
    case command_type::monitoring_set:
      out = pb::COMMAND_TYPE_MONITORING_SET;
      return true;
    case command_type::params_get:
      out = pb::COMMAND_TYPE_PARAMS_GET;
      return true;
    case command_type::params_set:
      out = pb::COMMAND_TYPE_PARAMS_SET;
      return true;
  }
  return false;
}

bool decode_command_type(pb::CommandType in, command_type& out) {
  switch (in) {
    case pb::COMMAND_TYPE_WOL_WAKE:
      out = command_type::wol_wake;
      return true;
    case pb::COMMAND_TYPE_HOST_REBOOT:
      out = command_type::host_reboot;
      return true;
    case pb::COMMAND_TYPE_HOST_POWEROFF:
      out = command_type::host_poweroff;
      return true;
    case pb::COMMAND_TYPE_HOST_PROBE_GET:
      out = command_type::host_probe_get;
      return true;
    case pb::COMMAND_TYPE_MONITORING_SET:
      out = command_type::monitoring_set;
      return true;
    case pb::COMMAND_TYPE_PARAMS_GET:
      out = command_type::params_get;
      return true;
    case pb::COMMAND_TYPE_PARAMS_SET:
      out = command_type::params_set;
      return true;
    default:
      return false;
  }
}

bool encode_command_status(command_status in, pb::CommandStatus& out) {
  switch (in) {
    case command_status::created:
      out = pb::COMMAND_STATUS_CREATED;
      return true;
    case command_status::dispatched:
      out = pb::COMMAND_STATUS_DISPATCHED;
      return true;
    case command_status::acked:
      out = pb::COMMAND_STATUS_ACKED;
      return true;
    case command_status::running:
      out = pb::COMMAND_STATUS_RUNNING;
      return true;
    case command_status::succeeded:
      out = pb::COMMAND_STATUS_SUCCEEDED;
      return true;
    case command_status::failed:
      out = pb::COMMAND_STATUS_FAILED;
      return true;
    case command_status::timed_out:
      out = pb::COMMAND_STATUS_TIMED_OUT;
      return true;
    case command_status::cancelled:
      out = pb::COMMAND_STATUS_CANCELLED;
      return true;
  }
  return false;
}

bool decode_command_status(pb::CommandStatus in, command_status& out) {
  switch (in) {
    case pb::COMMAND_STATUS_CREATED:
      out = command_status::created;
      return true;
    case pb::COMMAND_STATUS_DISPATCHED:
      out = command_status::dispatched;
      return true;
    case pb::COMMAND_STATUS_ACKED:
      out = command_status::acked;
      return true;
    case pb::COMMAND_STATUS_RUNNING:
      out = command_status::running;
      return true;
    case pb::COMMAND_STATUS_SUCCEEDED:
      out = command_status::succeeded;
      return true;
    case pb::COMMAND_STATUS_FAILED:
      out = command_status::failed;
      return true;
    case pb::COMMAND_STATUS_TIMED_OUT:
      out = command_status::timed_out;
      return true;
    case pb::COMMAND_STATUS_CANCELLED:
      out = command_status::cancelled;
      return true;
    default:
      return false;
  }
}

void encode_command_body(const command& input, pb::Command& out) {
  out.set_command_id(input.command_id);
  out.set_idempotency_key(input.idempotency_key);
  pb::CommandType command_type_value = pb::COMMAND_TYPE_UNSPECIFIED;
  if (encode_command_type(input.type, command_type_value)) {
    out.set_command_type(command_type_value);
  }
  out.set_issued_at_ms(input.issued_at_ms);
  out.set_expires_at_ms(input.expires_at_ms);
  out.set_timeout_ms(input.timeout_ms);
  out.set_max_retry(input.max_retry);
  out.set_payload_json(input.payload_json);
}

bool decode_command_body(const pb::Command& input, command& out, std::string& error) {
  out.command_id = input.command_id();
  out.idempotency_key = input.idempotency_key();
  out.issued_at_ms = input.issued_at_ms();
  out.expires_at_ms = input.expires_at_ms();
  out.timeout_ms = input.timeout_ms();
  out.max_retry = input.max_retry();
  out.payload_json = input.payload_json();

  if (!decode_command_type(input.command_type(), out.type)) {
    error = "invalid command_type";
    return false;
  }
  return true;
}

} // namespace

bool encode_envelope_proto(const envelope& input, pb::Envelope& out, std::string& error) {
  out.Clear();
  out.set_message_id(input.message_id);
  out.set_protocol_version(input.protocol_version);
  out.set_sent_at_ms(input.sent_at_ms);
  out.set_trace_id(input.trace_id);
  out.set_agent_id(input.agent_id);

  pb::MessageType message_type_value = pb::MESSAGE_TYPE_UNSPECIFIED;
  if (!encode_message_type(input.type, message_type_value)) {
    error = "invalid message_type";
    return false;
  }
  out.set_message_type(message_type_value);

  pb::ChannelType channel_type_value = pb::CHANNEL_TYPE_UNSPECIFIED;
  if (!encode_channel_type(input.channel, channel_type_value)) {
    error = "invalid channel_type";
    return false;
  }
  out.set_channel_type(channel_type_value);

  switch (input.type) {
    case message_type::register_agent: {
      const auto* p = std::get_if<register_payload>(&input.payload);
      if (!p) {
        error = "register payload missing";
        return false;
      }
      auto* body = out.mutable_register_();
      body->set_agent_id(p->agent_id);
      body->set_site_id(p->site_id);
      body->set_agent_version(p->agent_version);
      for (const auto& capability : p->capabilities) {
        body->add_capabilities(capability);
      }
      return true;
    }

    case message_type::register_ack: {
      const auto* p = std::get_if<register_ack_payload>(&input.payload);
      if (!p) {
        error = "register_ack payload missing";
        return false;
      }
      auto* body = out.mutable_register_ack();
      body->set_ok(p->ok);
      body->set_message(p->message);
      return true;
    }

    case message_type::heartbeat: {
      const auto* p = std::get_if<heartbeat_payload>(&input.payload);
      if (!p) {
        error = "heartbeat payload missing";
        return false;
      }
      auto* body = out.mutable_heartbeat();
      body->set_heartbeat_at_ms(p->heartbeat_at_ms);
      body->set_stats_json(p->stats_json);
      return true;
    }

    case message_type::heartbeat_ack: {
      const auto* p = std::get_if<heartbeat_ack_payload>(&input.payload);
      if (!p) {
        error = "heartbeat_ack payload missing";
        return false;
      }
      auto* body = out.mutable_heartbeat_ack();
      body->set_server_time_ms(p->server_time_ms);
      return true;
    }

    case message_type::command_push: {
      const auto* p = std::get_if<command>(&input.payload);
      if (!p) {
        error = "command_push payload missing";
        return false;
      }
      encode_command_body(*p, *out.mutable_command_push()->mutable_command());
      return true;
    }

    case message_type::command_ack: {
      const auto* p = std::get_if<command_ack_payload>(&input.payload);
      if (!p) {
        error = "command_ack payload missing";
        return false;
      }
      auto* body = out.mutable_command_ack();
      body->set_command_id(p->command_id);
      pb::CommandStatus status = pb::COMMAND_STATUS_UNSPECIFIED;
      if (!encode_command_status(p->status, status)) {
        error = "invalid command_ack status";
        return false;
      }
      body->set_status(status);
      body->set_message(p->message);
      return true;
    }

    case message_type::command_result: {
      const auto* p = std::get_if<command_result_payload>(&input.payload);
      if (!p) {
        error = "command_result payload missing";
        return false;
      }
      auto* body = out.mutable_command_result();
      body->set_command_id(p->command_id);
      pb::CommandStatus status = pb::COMMAND_STATUS_UNSPECIFIED;
      if (!encode_command_status(p->final_status, status)) {
        error = "invalid command_result status";
        return false;
      }
      body->set_final_status(status);
      body->set_exit_code(p->exit_code);
      body->set_result_json(p->result_json);
      return true;
    }

    case message_type::error: {
      const auto* p = std::get_if<error_payload>(&input.payload);
      if (!p) {
        error = "error payload missing";
        return false;
      }
      auto* body = out.mutable_error();
      body->set_code(p->code);
      body->set_message(p->message);
      body->set_detail(p->detail);
      return true;
    }
  }

  error = "unsupported message type";
  return false;
}

bool decode_envelope_proto(const pb::Envelope& input, envelope& out, std::string& error) {
  out = envelope{};
  out.message_id = input.message_id();
  out.protocol_version = input.protocol_version();
  out.sent_at_ms = input.sent_at_ms();
  out.trace_id = input.trace_id();
  out.agent_id = input.agent_id();

  if (!decode_message_type(input.message_type(), out.type)) {
    error = "invalid message_type";
    return false;
  }
  if (!decode_channel_type(input.channel_type(), out.channel)) {
    error = "invalid channel_type";
    return false;
  }

  switch (input.payload_case()) {
    case pb::Envelope::kRegister: {
      register_payload payload;
      payload.agent_id = input.register_().agent_id();
      payload.site_id = input.register_().site_id();
      payload.agent_version = input.register_().agent_version();
      payload.capabilities.reserve(input.register_().capabilities_size());
      for (const auto& capability : input.register_().capabilities()) {
        payload.capabilities.push_back(capability);
      }
      out.payload = std::move(payload);
      return true;
    }

    case pb::Envelope::kRegisterAck: {
      register_ack_payload payload;
      payload.ok = input.register_ack().ok();
      payload.message = input.register_ack().message();
      out.payload = std::move(payload);
      return true;
    }

    case pb::Envelope::kHeartbeat: {
      heartbeat_payload payload;
      payload.heartbeat_at_ms = input.heartbeat().heartbeat_at_ms();
      payload.stats_json = input.heartbeat().stats_json();
      out.payload = std::move(payload);
      return true;
    }

    case pb::Envelope::kHeartbeatAck: {
      heartbeat_ack_payload payload;
      payload.server_time_ms = input.heartbeat_ack().server_time_ms();
      out.payload = std::move(payload);
      return true;
    }

    case pb::Envelope::kCommandPush: {
      command payload;
      if (!decode_command_body(input.command_push().command(), payload, error)) {
        return false;
      }
      out.payload = std::move(payload);
      return true;
    }

    case pb::Envelope::kCommandAck: {
      command_ack_payload payload;
      payload.command_id = input.command_ack().command_id();
      if (!decode_command_status(input.command_ack().status(), payload.status)) {
        error = "invalid command_ack status";
        return false;
      }
      payload.message = input.command_ack().message();
      out.payload = std::move(payload);
      return true;
    }

    case pb::Envelope::kCommandResult: {
      command_result_payload payload;
      payload.command_id = input.command_result().command_id();
      if (!decode_command_status(input.command_result().final_status(), payload.final_status)) {
        error = "invalid command_result status";
        return false;
      }
      payload.exit_code = input.command_result().exit_code();
      payload.result_json = input.command_result().result_json();
      out.payload = std::move(payload);
      return true;
    }

    case pb::Envelope::kError: {
      error_payload payload;
      payload.code = input.error().code();
      payload.message = input.error().message();
      payload.detail = input.error().detail();
      out.payload = std::move(payload);
      return true;
    }

    case pb::Envelope::PAYLOAD_NOT_SET:
      out.payload = std::monostate{};
      return true;
  }

  error = "unknown payload case";
  return false;
}

} // namespace control

#endif
