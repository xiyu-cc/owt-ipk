#include "control/agent_runtime.h"

#include "log.h"
#include "service/command_store.h"
#include "service/host_probe_agent.h"
#include "service/params_store.h"
#include "service/ssh_executor.h"
#include "service/wakeonlan_sender.h"

#include <nlohmann/json.hpp>

#include <limits>
#include <string>

namespace control {

namespace {

constexpr const char* kDefaultSiteId = "default";
constexpr const char* kAgentVersion = "0.1.0";

bool update_int_field(
    const nlohmann::json& j,
    const char* key,
    int min_value,
    int max_value,
    int& target,
    std::string& error) {
  if (!j.contains(key)) {
    return true;
  }
  if (!j[key].is_number_integer()) {
    error = std::string("field ") + key + " must be integer";
    return false;
  }
  const int value = j[key].get<int>();
  if (value < min_value || value > max_value) {
    error = std::string("field ") + key + " out of range";
    return false;
  }
  target = value;
  return true;
}

bool update_string_field(
    const nlohmann::json& j,
    const char* key,
    std::string& target,
    std::string& error) {
  if (!j.contains(key)) {
    return true;
  }
  if (!j[key].is_string()) {
    error = std::string("field ") + key + " must be string";
    return false;
  }
  target = j[key].get<std::string>();
  return true;
}

nlohmann::json params_to_json(const service::control_params& params) {
  return {
      {"wol",
       {
           {"mac", params.wol.mac},
           {"broadcast", params.wol.broadcast},
           {"port", params.wol.port},
       }},
      {"ssh",
       {
           {"host", params.ssh.host},
           {"port", params.ssh.port},
           {"user", params.ssh.user},
           {"password", params.ssh.password},
           {"timeout_ms", params.ssh.timeout_ms},
       }},
  };
}

nlohmann::json probe_snapshot_to_json(const service::host_probe_snapshot& snap) {
  return {
      {"status", snap.status},
      {"monitoring_enabled", service::is_host_probe_monitoring_enabled()},
      {"message", snap.message},
      {"host", snap.host},
      {"port", snap.port},
      {"user", snap.user},
      {"cpu_usage_percent", snap.has_cpu_usage_percent ? nlohmann::json(snap.cpu_usage_percent) : nlohmann::json(nullptr)},
      {"mem_total_kb", snap.has_mem_total_kb ? nlohmann::json(snap.mem_total_kb) : nlohmann::json(nullptr)},
      {"mem_available_kb", snap.has_mem_available_kb ? nlohmann::json(snap.mem_available_kb) : nlohmann::json(nullptr)},
      {"mem_used_percent", snap.has_mem_used_percent ? nlohmann::json(snap.mem_used_percent) : nlohmann::json(nullptr)},
      {"net_rx_bytes", snap.has_net_rx_bytes ? nlohmann::json(snap.net_rx_bytes) : nlohmann::json(nullptr)},
      {"net_tx_bytes", snap.has_net_tx_bytes ? nlohmann::json(snap.net_tx_bytes) : nlohmann::json(nullptr)},
      {"net_rx_bytes_per_sec",
       snap.has_net_rx_bytes_per_sec ? nlohmann::json(snap.net_rx_bytes_per_sec) : nlohmann::json(nullptr)},
      {"net_tx_bytes_per_sec",
       snap.has_net_tx_bytes_per_sec ? nlohmann::json(snap.net_tx_bytes_per_sec) : nlohmann::json(nullptr)},
      {"sample_interval_ms",
       snap.has_sample_interval_ms ? nlohmann::json(snap.sample_interval_ms) : nlohmann::json(nullptr)},
      {"updated_at_ms", snap.updated_at_ms},
  };
}

} // namespace

agent_runtime::agent_runtime()
    : wss_channel_(std::make_unique<wss_control_channel>()),
      grpc_channel_(std::make_unique<grpc_control_channel>()) {}

agent_runtime::~agent_runtime() {
  stop();
}

bool agent_runtime::start(const agent_runtime_options& options) {
  if (running_.load(std::memory_order_relaxed)) {
    return true;
  }

  options_ = options;
  bool any_started = false;

  if (options_.enable_wss) {
    any_started = start_channel(*wss_channel_, options_.wss_endpoint) || any_started;
  }
  if (options_.enable_grpc) {
    any_started = start_channel(*grpc_channel_, options_.grpc_endpoint) || any_started;
  }

  if (!any_started) {
    log::error("agent runtime start failed: no control channel available");
    return false;
  }

  {
    std::lock_guard<std::mutex> lk(queue_mutex_);
    queue_.clear();
    execution_stop_ = false;
  }
  execution_thread_ = std::thread(&agent_runtime::execution_loop, this);

  primary_channel_ = select_primary_channel();
  if (primary_channel_ == nullptr) {
    {
      std::lock_guard<std::mutex> lk(queue_mutex_);
      execution_stop_ = true;
    }
    queue_cv_.notify_all();
    if (execution_thread_.joinable()) {
      execution_thread_.join();
    }
    log::error("agent runtime start failed: no primary channel selected");
    return false;
  }

  running_.store(true, std::memory_order_relaxed);
  log::info("agent runtime started, primary_channel={}", to_string(primary_channel_->type()));
  return send_register();
}

void agent_runtime::stop() {
  if (!running_.exchange(false, std::memory_order_relaxed)) {
    return;
  }

  if (wss_channel_) {
    wss_channel_->stop();
  }
  if (grpc_channel_) {
    grpc_channel_->stop();
  }
  {
    std::lock_guard<std::mutex> lk(queue_mutex_);
    execution_stop_ = true;
  }
  queue_cv_.notify_all();
  if (execution_thread_.joinable()) {
    execution_thread_.join();
  }
  primary_channel_ = nullptr;
  log::info("agent runtime stopped");
}

bool agent_runtime::is_running() const noexcept {
  return running_.load(std::memory_order_relaxed);
}

bool agent_runtime::send_register() {
  if (!is_running() || primary_channel_ == nullptr) {
    return false;
  }

  envelope message;
  message.message_id = make_message_id();
  message.type = message_type::register_agent;
  message.protocol_version = options_.protocol_version;
  message.channel = primary_channel_->type();
  message.sent_at_ms = unix_time_ms_now();
  message.agent_id = options_.agent_id;
  message.payload = register_payload{
      options_.agent_id,
      kDefaultSiteId,
      kAgentVersion,
      {"WOL_WAKE", "HOST_REBOOT", "HOST_POWEROFF", "HOST_PROBE_GET", "PARAMS_GET", "PARAMS_SET"}};
  return primary_channel_->send(message);
}

bool agent_runtime::send_heartbeat() {
  if (!is_running() || primary_channel_ == nullptr) {
    return false;
  }

  envelope message;
  message.message_id = make_message_id();
  message.type = message_type::heartbeat;
  message.protocol_version = options_.protocol_version;
  message.channel = primary_channel_->type();
  message.sent_at_ms = unix_time_ms_now();
  message.agent_id = options_.agent_id;
  message.payload = heartbeat_payload{unix_time_ms_now(), "{}"};
  return primary_channel_->send(message);
}

i_control_channel* agent_runtime::select_primary_channel() {
  const auto wss_running = wss_channel_ && wss_channel_->is_running();
  const auto grpc_running = grpc_channel_ && grpc_channel_->is_running();

  if (options_.prefer_wss) {
    if (wss_running) {
      return wss_channel_.get();
    }
    if (grpc_running) {
      return grpc_channel_.get();
    }
  } else {
    if (grpc_running) {
      return grpc_channel_.get();
    }
    if (wss_running) {
      return wss_channel_.get();
    }
  }
  return nullptr;
}

i_control_channel* agent_runtime::channel_for_type(channel_type type) {
  if (type == channel_type::wss) {
    return wss_channel_.get();
  }
  if (type == channel_type::grpc) {
    return grpc_channel_.get();
  }
  return nullptr;
}

channel_callbacks agent_runtime::build_callbacks(channel_type type) {
  channel_callbacks callbacks;
  callbacks.on_connected = [type]() {
    log::info("control channel connected: {}", to_string(type));
  };
  callbacks.on_disconnected = [type]() {
    log::warn("control channel disconnected: {}", to_string(type));
  };
  callbacks.on_error = [type](const std::string& err) {
    log::error("control channel error ({}): {}", to_string(type), err);
  };
  callbacks.on_message = [this, type](const envelope& message) {
    log::info(
        "control channel message received ({}): id={}, type={}",
        to_string(type),
        message.message_id,
        to_string(message.type));
    handle_channel_message(type, message);
  };
  return callbacks;
}

bool agent_runtime::start_channel(i_control_channel& channel, const std::string& endpoint) {
  channel_start_options start_options;
  start_options.agent_id = options_.agent_id;
  start_options.endpoint = endpoint;
  start_options.protocol_version = options_.protocol_version;
  start_options.auth_token = options_.management_token;

  const auto ok = channel.start(start_options, build_callbacks(channel.type()));
  if (!ok) {
    log::warn("failed to start channel={}", to_string(channel.type()));
  }
  return ok;
}

void agent_runtime::handle_channel_message(channel_type type, const envelope& message) {
  if (message.type != message_type::command_push) {
    return;
  }

  const auto* cmd = std::get_if<command>(&message.payload);
  if (cmd == nullptr) {
    log::warn("command_push payload missing command body");
    return;
  }

  persist_command_push(type, *cmd);

  envelope ack;
  ack.message_id = make_message_id();
  ack.type = message_type::command_ack;
  ack.protocol_version = options_.protocol_version;
  ack.channel = type;
  ack.sent_at_ms = unix_time_ms_now();
  ack.trace_id = message.trace_id;
  ack.agent_id = options_.agent_id;
  ack.payload = command_ack_payload{cmd->command_id, command_status::acked, "accepted"};

  auto* channel = channel_for_type(type);
  if (channel == nullptr || !channel->is_running()) {
    log::warn("command ack skipped: channel unavailable ({})", to_string(type));
    return;
  }
  if (!channel->send(ack)) {
    log::warn("command ack send failed: command_id={}", cmd->command_id);
    return;
  }
  persist_command_ack(type, cmd->command_id);
  enqueue_command(type, message.trace_id, *cmd);
}

void agent_runtime::persist_command_push(channel_type channel, const command& cmd) {
  const auto now = unix_time_ms_now();

  service::command_record record;
  record.command_id = cmd.command_id;
  record.idempotency_key = cmd.idempotency_key;
  record.command_type = to_string(cmd.type);
  record.status = "DISPATCHED";
  record.channel_type = to_string(channel);
  record.payload_json = cmd.payload_json;
  record.result_json = "";
  record.created_at_ms = now;
  record.updated_at_ms = now;

  std::string error;
  if (!service::upsert_command(record, error)) {
    log::warn("persist command failed: command_id={}, err={}", cmd.command_id, error);
    return;
  }

  nlohmann::json detail = {
      {"event", "COMMAND_PUSH"},
      {"command_type", to_string(cmd.type)},
      {"timeout_ms", cmd.timeout_ms},
      {"max_retry", cmd.max_retry},
  };
  error.clear();
  if (!service::append_command_event(
          cmd.command_id,
          "COMMAND_PUSH_RECEIVED",
          "DISPATCHED",
          to_string(channel),
          detail.dump(),
          now,
          error)) {
    log::warn("persist command event failed: command_id={}, err={}", cmd.command_id, error);
  }
}

void agent_runtime::persist_command_ack(channel_type channel, const std::string& command_id) {
  const auto now = unix_time_ms_now();
  std::string error;
  if (!service::update_command_status(
          command_id,
          "ACKED",
          to_string(channel),
          "",
          now,
          error)) {
    log::warn("persist ack status failed: command_id={}, err={}", command_id, error);
  }

  nlohmann::json detail = {
      {"event", "COMMAND_ACK"},
      {"message", "accepted"},
  };
  error.clear();
  if (!service::append_command_event(
          command_id,
          "COMMAND_ACK_SENT",
          "ACKED",
          to_string(channel),
          detail.dump(),
          now,
          error)) {
    log::warn("persist ack event failed: command_id={}, err={}", command_id, error);
  }
}

void agent_runtime::execute_command(channel_type channel, const std::string& trace_id, const command& cmd) {
  nlohmann::json payload = nlohmann::json::object();
  if (!cmd.payload_json.empty()) {
    try {
      payload = nlohmann::json::parse(cmd.payload_json);
    } catch (const std::exception&) {
      payload = nlohmann::json::object();
    }
  }
  if (!payload.is_object()) {
    payload = nlohmann::json::object();
  }

  command_status final_status = command_status::failed;
  int exit_code = -1;
  nlohmann::json result = nlohmann::json::object();

  switch (cmd.type) {
    case command_type::wol_wake: {
      auto params = service::load_control_params();
      service::wakeonlan_request req;
      req.mac = payload.value("mac", params.wol.mac);
      req.broadcast_ip = payload.value("broadcast", params.wol.broadcast);
      req.port = payload.value("port", params.wol.port);

      const auto res = service::send_magic_packet(req);
      if (res.ok) {
        final_status = command_status::succeeded;
        exit_code = 0;
      } else {
        final_status = command_status::failed;
        exit_code = -1;
      }
      result = {
          {"ok", res.ok},
          {"error", res.error},
          {"bytes_sent", res.bytes_sent},
          {"mac", req.mac},
          {"broadcast", req.broadcast_ip},
          {"port", req.port},
      };
      break;
    }

    case command_type::host_reboot:
    case command_type::host_poweroff: {
      auto params = service::load_control_params();
      service::ssh_request req;
      req.host = payload.value("host", params.ssh.host);
      req.port = payload.value("port", params.ssh.port);
      req.user = payload.value("user", payload.value("username", params.ssh.user));
      req.password = payload.value("password", params.ssh.password);
      req.timeout_ms = payload.value("timeout_ms", params.ssh.timeout_ms);
      req.command = (cmd.type == command_type::host_reboot) ? "reboot" : "poweroff";

      const auto res = service::run_ssh_command(req);
      final_status = res.ok ? command_status::succeeded : command_status::failed;
      exit_code = res.exit_status;
      result = {
          {"ok", res.ok},
          {"error", res.error},
          {"output", res.output},
          {"exit_status", res.exit_status},
          {"host", req.host},
          {"port", req.port},
          {"user", req.user},
          {"command", req.command},
      };
      break;
    }

    case command_type::host_probe_get: {
      final_status = command_status::succeeded;
      exit_code = 0;
      result = probe_snapshot_to_json(service::get_host_probe_snapshot());
      break;
    }

    case command_type::monitoring_set: {
      if (!payload.contains("enabled") || !payload["enabled"].is_boolean()) {
        final_status = command_status::failed;
        exit_code = -1;
        result = {{"error", "field enabled must be boolean"}};
      } else {
        service::set_host_probe_monitoring_enabled(payload["enabled"].get<bool>());
        final_status = command_status::succeeded;
        exit_code = 0;
        result = {{"enabled", service::is_host_probe_monitoring_enabled()}};
      }
      break;
    }

    case command_type::params_get: {
      final_status = command_status::succeeded;
      exit_code = 0;
      result = params_to_json(service::load_control_params());
      break;
    }

    case command_type::params_set: {
      auto params = service::load_control_params();
      std::string field_error;
      bool ok = true;

      if (payload.contains("wol")) {
        if (!payload["wol"].is_object()) {
          ok = false;
          field_error = "field wol must be object";
        } else {
          const auto& wol = payload["wol"];
          ok = update_string_field(wol, "mac", params.wol.mac, field_error) &&
               update_string_field(wol, "broadcast", params.wol.broadcast, field_error) &&
               update_int_field(wol, "port", 1, 65535, params.wol.port, field_error);
        }
      }

      if (ok && payload.contains("ssh")) {
        if (!payload["ssh"].is_object()) {
          ok = false;
          field_error = "field ssh must be object";
        } else {
          const auto& ssh = payload["ssh"];
          ok = update_string_field(ssh, "host", params.ssh.host, field_error) &&
               update_int_field(ssh, "port", 1, 65535, params.ssh.port, field_error) &&
               update_string_field(ssh, "user", params.ssh.user, field_error) &&
               update_string_field(ssh, "password", params.ssh.password, field_error) &&
               update_int_field(
                   ssh, "timeout_ms", 100, std::numeric_limits<int>::max(), params.ssh.timeout_ms, field_error);
        }
      }

      if (!ok) {
        final_status = command_status::failed;
        exit_code = -1;
        result = {{"error", field_error}};
        break;
      }

      std::string save_error;
      if (!service::save_control_params(params, save_error)) {
        final_status = command_status::failed;
        exit_code = -1;
        result = {{"error", save_error}};
        break;
      }

      final_status = command_status::succeeded;
      exit_code = 0;
      result = params_to_json(params);
      break;
    }
  }

  const auto result_json = result.dump();
  send_command_result(channel, trace_id, cmd.command_id, final_status, exit_code, result_json);
  persist_command_result(channel, cmd.command_id, final_status, exit_code, result_json);
}

bool agent_runtime::send_command_result(
    channel_type channel,
    const std::string& trace_id,
    const std::string& command_id,
    command_status final_status,
    int exit_code,
    const std::string& result_json) {
  auto* ch = channel_for_type(channel);
  if (ch == nullptr || !ch->is_running()) {
    log::warn("send command result skipped: channel unavailable ({})", to_string(channel));
    return false;
  }

  envelope message;
  message.message_id = make_message_id();
  message.type = message_type::command_result;
  message.protocol_version = options_.protocol_version;
  message.channel = channel;
  message.sent_at_ms = unix_time_ms_now();
  message.trace_id = trace_id;
  message.agent_id = options_.agent_id;
  message.payload = command_result_payload{command_id, final_status, exit_code, result_json};
  return ch->send(message);
}

void agent_runtime::persist_command_result(
    channel_type channel,
    const std::string& command_id,
    command_status final_status,
    int exit_code,
    const std::string& result_json) {
  const auto now = unix_time_ms_now();
  std::string error;
  if (!service::update_command_status(
          command_id, to_string(final_status), to_string(channel), result_json, now, error)) {
    log::warn("persist result status failed: command_id={}, err={}", command_id, error);
  }

  nlohmann::json detail = {
      {"event", "COMMAND_RESULT"},
      {"exit_code", exit_code},
  };
  error.clear();
  if (!service::append_command_event(
          command_id,
          "COMMAND_RESULT_SENT",
          to_string(final_status),
          to_string(channel),
          detail.dump(),
          now,
          error)) {
    log::warn("persist result event failed: command_id={}, err={}", command_id, error);
  }
}

void agent_runtime::enqueue_command(
    channel_type channel, const std::string& trace_id, const command& cmd) {
  {
    std::lock_guard<std::mutex> lk(queue_mutex_);
    queue_.push_back(queued_command{channel, trace_id, cmd});
  }
  queue_cv_.notify_one();
}

void agent_runtime::execution_loop() {
  while (true) {
    queued_command item;
    {
      std::unique_lock<std::mutex> lk(queue_mutex_);
      queue_cv_.wait(lk, [this]() { return execution_stop_ || !queue_.empty(); });
      if (execution_stop_ && queue_.empty()) {
        break;
      }
      item = std::move(queue_.front());
      queue_.pop_front();
    }
    execute_command(item.channel, item.trace_id, item.cmd);
  }
}

} // namespace control
