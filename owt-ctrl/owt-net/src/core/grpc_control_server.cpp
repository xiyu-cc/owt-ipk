#include "server/grpc_control_server.h"

#include "control/control_protocol.h"
#include "control/grpc_codec.h"
#include "log.h"
#include "service/command_store.h"
#include "service/control_hub.h"

#if OWT_CTRL_ENABLE_GRPC_TRANSPORT
#include "control_channel.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#endif

#include <memory>
#include <thread>
#include <utility>

namespace server {

#if OWT_CTRL_ENABLE_GRPC_TRANSPORT

namespace {

namespace pb = owt::control::v1;

bool is_terminal_command_status(const std::string& status) {
  return status == "SUCCEEDED" || status == "FAILED" || status == "TIMED_OUT" ||
         status == "CANCELLED";
}

class grpc_stream_session final : public service::i_control_session {
public:
  explicit grpc_stream_session(grpc::ServerReaderWriter<pb::Envelope, pb::Envelope>* stream)
      : stream_(stream) {}

  bool send_control_message(const control::envelope& message) override {
    pb::Envelope out;
    std::string error;
    if (!control::encode_envelope_proto(message, out, error)) {
      log::warn("grpc encode envelope failed: {}", error);
      return false;
    }

    std::lock_guard<std::mutex> lock(write_mutex_);
    if (closed_ || stream_ == nullptr) {
      return false;
    }
    return stream_->Write(out);
  }

  void close() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    closed_ = true;
  }

private:
  grpc::ServerReaderWriter<pb::Envelope, pb::Envelope>* stream_ = nullptr;
  std::mutex write_mutex_;
  bool closed_ = false;
};

class control_channel_service_impl final : public pb::ControlChannelService::Service {
public:
  grpc::Status Connect(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<pb::Envelope, pb::Envelope>* stream) override {
    (void)context;
    auto session = std::make_shared<grpc_stream_session>(stream);

    std::string agent_id;
    pb::Envelope in;
    while (stream->Read(&in)) {
      control::envelope message;
      std::string decode_error;
      if (!control::decode_envelope_proto(in, message, decode_error)) {
        log::warn("grpc decode envelope failed: {}", decode_error);
        continue;
      }
      handle_message(message, agent_id, session);
    }

    if (!agent_id.empty()) {
      service::unregister_grpc_control_session(agent_id, session.get());
    }
    session->close();
    return grpc::Status::OK;
  }

private:
  static void handle_command_ack(const control::envelope& message, const std::string& current_agent_id) {
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
            "grpc",
            "",
            now,
            db_error)) {
      log::warn("persist grpc ack status failed: command_id={}, err={}", payload->command_id, db_error);
    }

    nlohmann::json detail = {
        {"event", "COMMAND_ACK"},
        {"agent_id", !current_agent_id.empty() ? current_agent_id : message.agent_id},
        {"message", payload->message},
    };
    db_error.clear();
    if (!service::append_command_event(
            payload->command_id,
            "COMMAND_ACK_RECEIVED",
            control::to_string(payload->status),
            "grpc",
            detail.dump(),
            now,
            db_error)) {
      log::warn("persist grpc ack event failed: command_id={}, err={}", payload->command_id, db_error);
    }
  }

  static void handle_command_result(const control::envelope& message, const std::string& current_agent_id) {
    const auto* payload = std::get_if<control::command_result_payload>(&message.payload);
    if (payload == nullptr || payload->command_id.empty()) {
      return;
    }

    std::string db_error;
    const auto now = control::unix_time_ms_now();
    if (!service::update_command_status(
            payload->command_id,
            control::to_string(payload->final_status),
            "grpc",
            payload->result_json,
            now,
            db_error)) {
      log::warn(
          "persist grpc result status failed: command_id={}, err={}", payload->command_id, db_error);
    }

    nlohmann::json detail = {
        {"event", "COMMAND_RESULT"},
        {"agent_id", !current_agent_id.empty() ? current_agent_id : message.agent_id},
        {"exit_code", payload->exit_code},
    };
    db_error.clear();
    if (!service::append_command_event(
            payload->command_id,
            "COMMAND_RESULT_RECEIVED",
            control::to_string(payload->final_status),
            "grpc",
            detail.dump(),
            now,
            db_error)) {
      log::warn(
          "persist grpc result event failed: command_id={}, err={}", payload->command_id, db_error);
    }
  }

  static void handle_message(
      const control::envelope& message,
      std::string& agent_id,
      const std::shared_ptr<grpc_stream_session>& session) {
    switch (message.type) {
      case control::message_type::register_agent: {
        const auto* payload = std::get_if<control::register_payload>(&message.payload);
        if (payload == nullptr || payload->agent_id.empty()) {
          log::warn("grpc register payload missing agent_id");
          return;
        }

        if (!agent_id.empty() && agent_id != payload->agent_id) {
          service::unregister_grpc_control_session(agent_id, session.get());
        }
        agent_id = payload->agent_id;
        service::register_grpc_control_session(agent_id, session);

        control::envelope ack;
        ack.message_id = control::make_message_id();
        ack.type = control::message_type::register_ack;
        ack.protocol_version = message.protocol_version;
        ack.channel = control::channel_type::grpc;
        ack.sent_at_ms = control::unix_time_ms_now();
        ack.trace_id = message.trace_id;
        ack.agent_id = agent_id;
        ack.payload = control::register_ack_payload{true, "registered"};
        if (!session->send_control_message(ack)) {
          log::warn("grpc send register_ack failed: agent_id={}", agent_id);
        }
        break;
      }

      case control::message_type::heartbeat: {
        if (!message.agent_id.empty() && message.agent_id != agent_id) {
          if (!agent_id.empty()) {
            service::unregister_grpc_control_session(agent_id, session.get());
          }
          agent_id = message.agent_id;
          service::register_grpc_control_session(agent_id, session);
        }

        control::envelope ack;
        ack.message_id = control::make_message_id();
        ack.type = control::message_type::heartbeat_ack;
        ack.protocol_version = message.protocol_version;
        ack.channel = control::channel_type::grpc;
        ack.sent_at_ms = control::unix_time_ms_now();
        ack.trace_id = message.trace_id;
        ack.agent_id = agent_id;
        ack.payload = control::heartbeat_ack_payload{ack.sent_at_ms};
        if (!session->send_control_message(ack)) {
          log::warn("grpc send heartbeat_ack failed: agent_id={}", agent_id);
        }
        break;
      }

      case control::message_type::command_ack:
        handle_command_ack(message, agent_id);
        break;

      case control::message_type::command_result:
        handle_command_result(message, agent_id);
        break;

      default:
        break;
    }
  }
};

} // namespace

#endif

class grpc_control_server::impl {
public:
  bool running = false;
#if OWT_CTRL_ENABLE_GRPC_TRANSPORT
  std::unique_ptr<control_channel_service_impl> service;
  std::unique_ptr<grpc::Server> server;
  std::thread worker;
#endif
};

grpc_control_server::grpc_control_server() : impl_(std::make_unique<impl>()) {}

grpc_control_server::~grpc_control_server() {
  stop();
}

bool grpc_control_server::start(const std::string& endpoint) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (impl_->running) {
    return true;
  }

  if (endpoint.empty()) {
    log::warn("grpc control server start failed: empty endpoint");
    return false;
  }

#if OWT_CTRL_ENABLE_GRPC_TRANSPORT
  auto service = std::make_unique<control_channel_service_impl>();
  grpc::ServerBuilder builder;
  builder.AddListeningPort(endpoint, grpc::InsecureServerCredentials());
  builder.RegisterService(service.get());
  auto server = builder.BuildAndStart();
  if (!server) {
    log::error("grpc control server start failed: endpoint={}", endpoint);
    return false;
  }

  impl_->service = std::move(service);
  impl_->server = std::move(server);
  auto* server_ptr = impl_->server.get();
  impl_->worker = std::thread([server_ptr]() { server_ptr->Wait(); });
  impl_->running = true;
  log::info("grpc control server started: endpoint={}", endpoint);
  return true;
#else
  (void)endpoint;
  log::warn("grpc control server start skipped: grpc transport disabled at build time");
  return false;
#endif
}

void grpc_control_server::stop() {
#if OWT_CTRL_ENABLE_GRPC_TRANSPORT
  std::unique_ptr<grpc::Server> server;
  std::thread worker;
  std::unique_ptr<control_channel_service_impl> service;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!impl_->running) {
      return;
    }
    impl_->running = false;
    server = std::move(impl_->server);
    worker = std::move(impl_->worker);
    service = std::move(impl_->service);
  }

  if (server) {
    server->Shutdown();
  }
  if (worker.joinable()) {
    worker.join();
  }
  service.reset();
  log::info("grpc control server stopped");
#endif
}

bool grpc_control_server::is_running() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return impl_->running;
}

} // namespace server
