#include "control/agent_runtime.h"

#include "log.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace control {

namespace {

constexpr const char* kDefaultSiteId = "default";
constexpr const char* kAgentVersion = "0.2.0";

bool is_command_expired(const command& cmd, int64_t now_ms) {
  return cmd.expires_at_ms > 0 && now_ms >= cmd.expires_at_ms;
}

std::string request_id_to_text(const nlohmann::json& request_id) {
  if (request_id.is_null()) {
    return "";
  }
  if (request_id.is_string()) {
    return request_id.get<std::string>();
  }
  if (request_id.is_number_integer()) {
    return std::to_string(request_id.get<int64_t>());
  }
  if (request_id.is_number_unsigned()) {
    return std::to_string(request_id.get<uint64_t>());
  }
  return request_id.dump();
}

} // namespace

agent_runtime::agent_runtime()
    : agent_runtime(std::make_unique<wss_control_channel>()) {}

agent_runtime::agent_runtime(std::unique_ptr<i_control_channel> channel)
    : agent_runtime(
          std::move(channel),
          std::make_shared<agent_command_executor_registry>(),
          agent_runtime_heartbeat_builder{}) {}

agent_runtime::agent_runtime(
    std::unique_ptr<i_control_channel> channel,
    std::shared_ptr<agent_command_executor_registry> executor_registry,
    agent_runtime_heartbeat_builder heartbeat_builder)
    : wss_channel_(channel ? std::move(channel) : std::make_unique<wss_control_channel>()),
      executor_registry_(executor_registry ? std::move(executor_registry)
                                           : std::make_shared<agent_command_executor_registry>()),
      heartbeat_builder_(std::move(heartbeat_builder)),
      message_router_(current_protocol_version()) {}

agent_runtime::~agent_runtime() {
  stop();
}

bool agent_runtime::start(const agent_runtime_options& options) {
  if (running_.load(std::memory_order_relaxed)) {
    return true;
  }

  options_ = options;
  running_.store(true, std::memory_order_relaxed);

  runtime_event_dispatcher_options dispatcher_options;
  dispatcher_options.workers = std::max(1, options_.ws_event_workers);
  dispatcher_options.queue_capacity = static_cast<std::size_t>(std::max(64, options_.ws_event_queue_capacity));
  dispatcher_options.low_priority_drop_threshold_pct = 80;
  if (!event_dispatcher_.start(dispatcher_options)) {
    running_.store(false, std::memory_order_relaxed);
    log::error("agent runtime start failed: event dispatcher unavailable");
    return false;
  }

  if (!start_channel(*wss_channel_, options_.wss_endpoint)) {
    event_dispatcher_.stop();
    running_.store(false, std::memory_order_relaxed);
    log::error("agent runtime start failed: wss channel unavailable");
    return false;
  }

  {
    std::lock_guard<std::mutex> seen_lk(seen_commands_mutex_);
    seen_command_ids_.clear();
  }

  const auto worker_started = execution_worker_.start(
      [this](const nlohmann::json& request_id, const command& cmd) {
        if (!send_command_ack(request_id, cmd.command_id, command_status::running, "running")) {
          log::warn("command running ack send failed: command_id={}", cmd.command_id);
        }
      },
      [this](const nlohmann::json& request_id, const command& cmd) {
        execute_command(request_id, cmd);
      },
      [this](const nlohmann::json& request_id, const command& cmd, std::exception_ptr exception_ptr) {
        on_execute_exception(request_id, cmd, exception_ptr);
      });
  if (!worker_started) {
    wss_channel_->stop();
    event_dispatcher_.stop();
    running_.store(false, std::memory_order_relaxed);
    log::error("agent runtime start failed: execution worker unavailable");
    return false;
  }

  log::info("agent runtime started");
  return true;
}

void agent_runtime::stop() {
  if (!running_.exchange(false, std::memory_order_relaxed)) {
    return;
  }

  if (wss_channel_) {
    wss_channel_->stop();
  }
  event_dispatcher_.stop();
  execution_worker_.stop();

  log::info("agent runtime stopped");
}

bool agent_runtime::is_running() const noexcept {
  return running_.load(std::memory_order_relaxed);
}

void agent_runtime::register_command_executor(command_type type, command_executor executor) {
  if (!executor_registry_) {
    return;
  }
  if (is_running()) {
    log::warn("register_command_executor ignored while runtime running");
    return;
  }
  executor_registry_->register_executor(type, std::move(executor));
}

bool agent_runtime::send_control_message(
    message_type type,
    const nlohmann::json& request_id,
    payload_variant data) {
  auto* ch = wss_channel_.get();
  if (ch == nullptr || !ch->is_running()) {
    log::warn("send message skipped: channel unavailable");
    return false;
  }

  envelope message;
  message.type = type;
  message.version = options_.protocol_version;
  message.id = request_id.is_null() ? nlohmann::json(make_message_id()) : request_id;
  message.ts_ms = unix_time_ms_now();
  message.target = options_.agent_mac;
  message.payload = std::move(data);
  return ch->send(message);
}

bool agent_runtime::send_register() {
  if (!is_running()) {
    return false;
  }

  return send_control_message(
      message_type::agent_register,
      make_message_id(),
      register_payload{
          options_.agent_mac,
          options_.agent_id,
          kDefaultSiteId,
          kAgentVersion,
          {"wol_wake",
           "host_reboot",
           "host_poweroff",
           "host_probe_get",
           "monitoring_set",
           "params_get",
           "params_set"}});
}

bool agent_runtime::send_heartbeat() {
  if (!is_running()) {
    return false;
  }

  return send_control_message(
      message_type::agent_heartbeat,
      make_message_id(),
      heartbeat_payload{unix_time_ms_now(), heartbeat_builder_.build_stats()});
}

bool agent_runtime::send_command_ack(
    const nlohmann::json& request_id,
    const std::string& command_id,
    command_status status,
    std::string_view message) {
  return send_control_message(
      message_type::agent_command_ack,
      request_id,
      command_ack_payload{command_id, status, std::string(message)});
}

channel_callbacks agent_runtime::build_callbacks(const std::string& endpoint) {
  channel_callbacks callbacks;
  callbacks.on_connected = [this, endpoint]() {
    const auto result = event_dispatcher_.post(
        "agent",
        runtime_event_priority::high,
        [this, endpoint]() {
          log::info("control channel connected: endpoint={}", endpoint);
          if (!send_register()) {
            log::warn("send register on connected failed: endpoint={}", endpoint);
          }
        });
    if (result != runtime_event_post_result::accepted) {
      log::warn("drop on_connected callback: endpoint={}, reason={}", endpoint, static_cast<int>(result));
    }
  };

  callbacks.on_disconnected = [this, endpoint]() {
    const auto result = event_dispatcher_.post(
        "agent",
        runtime_event_priority::high,
        [endpoint]() {
          log::warn("control channel disconnected: endpoint={}", endpoint);
        });
    if (result != runtime_event_post_result::accepted) {
      log::warn("drop on_disconnected callback: endpoint={}, reason={}", endpoint, static_cast<int>(result));
    }
  };

  callbacks.on_error = [this, endpoint](const std::string& err) {
    const auto result = event_dispatcher_.post(
        "agent",
        runtime_event_priority::high,
        [endpoint, err]() {
          log::error("control channel error (endpoint={}): {}", endpoint, err);
        });
    if (result != runtime_event_post_result::accepted) {
      log::warn("drop on_error callback: endpoint={}, reason={}", endpoint, static_cast<int>(result));
    }
  };

  callbacks.on_message = [this, endpoint](const envelope& message) {
    std::string partition_key = "agent";
    if (const auto* cmd = std::get_if<command>(&message.payload); cmd != nullptr && !cmd->command_id.empty()) {
      partition_key = cmd->command_id;
    } else {
      const auto request_id = request_id_to_text(message.id);
      if (!request_id.empty()) {
        partition_key = request_id;
      }
    }

    const auto result = event_dispatcher_.post(
        partition_key,
        runtime_event_priority::high,
        [this, endpoint, message]() {
          const auto request_id = request_id_to_text(message.id);
          log::info(
              "control channel message received (endpoint={}): id={}, name={}",
              endpoint,
              request_id,
              to_string(message.type));
          handle_channel_message(message);
        });
    if (result != runtime_event_post_result::accepted) {
      const auto request_id = request_id_to_text(message.id);
      log::warn(
          "drop on_message callback: endpoint={}, id={}, reason={}",
          endpoint,
          request_id,
          static_cast<int>(result));
    }
  };

  return callbacks;
}

bool agent_runtime::start_channel(i_control_channel& channel, const std::string& endpoint) {
  channel_start_options start_options;
  start_options.agent_id = options_.agent_id;
  start_options.endpoint = endpoint;
  start_options.protocol_version = options_.protocol_version;

  const auto ok = channel.start(start_options, build_callbacks(endpoint));
  if (!ok) {
    log::warn("failed to start control channel: endpoint={}", endpoint);
  }
  return ok;
}

bool agent_runtime::mark_command_seen(const std::string& command_id) {
  if (command_id.empty()) {
    return true;
  }
  std::lock_guard<std::mutex> lk(seen_commands_mutex_);
  const auto it = seen_command_ids_.find(command_id);
  if (it != seen_command_ids_.end()) {
    return false;
  }
  seen_command_ids_.insert(command_id);
  return true;
}

void agent_runtime::handle_channel_message(const envelope& message) {
  message_router_.route(
      message,
      agent_runtime_message_router::handlers{
          .on_unsupported_protocol = [this](const std::string& remote_version) {
            log::error(
                "control channel message ignored: unsupported protocol={}, local_version={}",
                remote_version,
                current_protocol_version());
          },
          .on_server_error = [](const error_payload& payload) {
            log::error(
                "control plane returned error: code={}, message={}, detail={}",
                payload.code,
                payload.message,
                payload.detail.dump());
          },
          .on_invalid_message = [](const std::string& reason) {
            log::warn("control channel invalid message: {}", reason);
          },
          .on_command_dispatch = [this](const nlohmann::json& request_id, const command& cmd) {
            if (!send_command_ack(request_id, cmd.command_id, command_status::acked, "accepted")) {
              log::warn("command ack send failed: command_id={}", cmd.command_id);
              return;
            }

            const bool should_execute = mark_command_seen(cmd.command_id);
            if (!should_execute) {
              log::info("duplicate command ignored: command_id={}", cmd.command_id);
              return;
            }

            const auto checked_at_ms = unix_time_ms_now();
            if (is_command_expired(cmd, checked_at_ms)) {
              nlohmann::json result = {
                  {"ok", false},
                  {"error_code", "command_expired"},
                  {"message", "command expired before execution"},
                  {"expires_at_ms", cmd.expires_at_ms},
                  {"checked_at_ms", checked_at_ms},
              };
              if (!send_command_result(
                      request_id,
                      cmd.command_id,
                      command_status::timed_out,
                      -1,
                      result)) {
                log::warn("send expired command result failed: command_id={}", cmd.command_id);
              }
              log::warn(
                  "command expired, skip execution: command_id={}, expires_at_ms={}, checked_at_ms={}",
                  cmd.command_id,
                  cmd.expires_at_ms,
                  checked_at_ms);
              return;
            }

            enqueue_command(request_id, cmd);
          },
      });
}

void agent_runtime::execute_command(const nlohmann::json& request_id, const command& cmd) {
  nlohmann::json payload = cmd.payload;
  if (!payload.is_object()) {
    payload = nlohmann::json::object();
  }

  command_execution_result execution;
  if (!executor_registry_ || !executor_registry_->execute(cmd, payload, execution)) {
    (void)send_command_result(
        request_id,
        cmd.command_id,
        command_status::failed,
        -1,
        nlohmann::json{{"error", "unsupported command_type"}});
    return;
  }

  (void)send_command_result(
      request_id,
      cmd.command_id,
      execution.status,
      execution.exit_code,
      execution.result);
}

void agent_runtime::on_execute_exception(
    const nlohmann::json& request_id,
    const command& cmd,
    std::exception_ptr exception_ptr) {
  if (exception_ptr == nullptr) {
    return;
  }

  try {
    std::rethrow_exception(exception_ptr);
  } catch (const std::exception& ex) {
    log::error("execute command failed with exception: command_id={}, err={}", cmd.command_id, ex.what());
    nlohmann::json result = {
        {"ok", false},
        {"error_code", "internal_error"},
        {"message", "command execution exception"},
    };
    result["detail"] = ex.what();
    if (!send_command_result(request_id, cmd.command_id, command_status::failed, -1, result)) {
      log::warn(
          "send command result after exception failed: command_id={}", cmd.command_id);
    }
  } catch (...) {
    log::error("execute command failed with unknown exception: command_id={}", cmd.command_id);
    nlohmann::json result = {
        {"ok", false},
        {"error_code", "internal_error"},
        {"message", "command execution exception"},
        {"detail", "unknown exception"},
    };
    if (!send_command_result(request_id, cmd.command_id, command_status::failed, -1, result)) {
      log::warn(
          "send command result after unknown exception failed: command_id={}", cmd.command_id);
    }
  }
}

bool agent_runtime::send_command_result(
    const nlohmann::json& request_id,
    const std::string& command_id,
    command_status final_status,
    int exit_code,
    const nlohmann::json& result) {
  return send_control_message(
      message_type::agent_command_result,
      request_id,
      command_result_payload{command_id, final_status, exit_code, result});
}

void agent_runtime::enqueue_command(const nlohmann::json& request_id, const command& cmd) {
  execution_worker_.enqueue(request_id, cmd);
}

} // namespace control
