#include "control/agent_runtime.h"

#include "log.h"
#include "service/host_probe_agent.h"
#include "service/params_store.h"
#include "service/ssh_executor.h"
#include "service/wakeonlan_sender.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace control {

namespace {

constexpr const char* kDefaultSiteId = "default";
constexpr const char* kAgentVersion = "0.2.0";

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
  int64_t parsed = 0;
  try {
    parsed = j[key].get<int64_t>();
  } catch (const std::exception&) {
    error = std::string("field ") + key + " must be integer";
    return false;
  }
  if (parsed < static_cast<int64_t>(std::numeric_limits<int>::min()) ||
      parsed > static_cast<int64_t>(std::numeric_limits<int>::max())) {
    error = std::string("field ") + key + " out of range";
    return false;
  }
  const int value = static_cast<int>(parsed);
  if (value < min_value || value > max_value) {
    error = std::string("field ") + key + " out of range";
    return false;
  }
  target = value;
  return true;
}

template <typename T>
T payload_value_or(const nlohmann::json& payload, const char* key, const T& fallback) {
  if (!payload.is_object()) {
    return fallback;
  }
  const auto it = payload.find(key);
  if (it == payload.end() || it->is_null()) {
    return fallback;
  }
  try {
    return it->get<T>();
  } catch (const std::exception&) {
    return fallback;
  }
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

nlohmann::json build_heartbeat_stats() {
  const bool monitoring_enabled = service::is_host_probe_monitoring_enabled();
  if (!monitoring_enabled) {
    return nlohmann::json(
        {{"monitoring_enabled", false}, {"status", "paused"}, {"message", "status collection disabled"}});
  }
  auto stats = probe_snapshot_to_json(service::get_host_probe_snapshot());
  stats["monitoring_enabled"] = true;
  return stats;
}

bool is_command_expired(const command& cmd, int64_t now_ms) {
  return cmd.expires_at_ms > 0 && now_ms >= cmd.expires_at_ms;
}

} // namespace

agent_runtime::agent_runtime() : wss_channel_(std::make_unique<wss_control_channel>()) {
  install_command_executors();
}

agent_runtime::~agent_runtime() {
  stop();
}

bool agent_runtime::start(const agent_runtime_options& options) {
  if (running_.load(std::memory_order_relaxed)) {
    return true;
  }

  options_ = options;
  running_.store(true, std::memory_order_relaxed);
  if (!start_channel(*wss_channel_, options_.wss_endpoint)) {
    running_.store(false, std::memory_order_relaxed);
    log::error("agent runtime start failed: wss channel unavailable");
    return false;
  }

  {
    std::lock_guard<std::mutex> seen_lk(seen_commands_mutex_);
    seen_command_ids_.clear();
  }
  {
    std::lock_guard<std::mutex> lk(queue_mutex_);
    queue_.clear();
    execution_stop_ = false;
  }
  execution_thread_ = std::thread(&agent_runtime::execution_loop, this);

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
  {
    std::lock_guard<std::mutex> lk(queue_mutex_);
    execution_stop_ = true;
  }
  queue_cv_.notify_all();
  if (execution_thread_.joinable()) {
    execution_thread_.join();
  }
  log::info("agent runtime stopped");
}

bool agent_runtime::is_running() const noexcept {
  return running_.load(std::memory_order_relaxed);
}

void agent_runtime::install_command_executors() {
  command_executors_.clear();

  command_executors_.emplace(command_type::wol_wake, [this](const command& cmd, const nlohmann::json& payload) {
    (void)cmd;
    command_execution_result out;
    auto params = service::load_control_params();
    service::wakeonlan_request req;
    req.mac = payload_value_or<std::string>(payload, "mac", params.wol.mac);
    req.broadcast_ip = payload_value_or<std::string>(payload, "broadcast", params.wol.broadcast);
    req.port = payload_value_or<int>(payload, "port", params.wol.port);

    const auto res = service::send_magic_packet(req);
    out.status = res.ok ? command_status::succeeded : command_status::failed;
    out.exit_code = res.ok ? 0 : -1;
    out.result = {
        {"ok", res.ok},
        {"error", res.error},
        {"bytes_sent", res.bytes_sent},
        {"mac", req.mac},
        {"broadcast", req.broadcast_ip},
        {"port", req.port},
    };
    return out;
  });

  command_executors_.emplace(command_type::host_reboot, [this](const command& cmd, const nlohmann::json& payload) {
    (void)cmd;
    command_execution_result out;
    auto params = service::load_control_params();
    service::ssh_request req;
    req.host = payload_value_or<std::string>(payload, "host", params.ssh.host);
    req.port = payload_value_or<int>(payload, "port", params.ssh.port);
    req.user = payload_value_or<std::string>(payload, "user", params.ssh.user);
    req.password = payload_value_or<std::string>(payload, "password", params.ssh.password);
    req.timeout_ms = payload_value_or<int>(payload, "timeout_ms", params.ssh.timeout_ms);
    req.command = "reboot";

    const auto res = service::run_ssh_command(req);
    out.status = res.ok ? command_status::succeeded : command_status::failed;
    out.exit_code = res.exit_status;
    out.result = {
        {"ok", res.ok},
        {"error", res.error},
        {"output", res.output},
        {"exit_status", res.exit_status},
        {"host", req.host},
        {"port", req.port},
        {"user", req.user},
        {"command", req.command},
    };
    return out;
  });

  command_executors_.emplace(command_type::host_poweroff, [this](const command& cmd, const nlohmann::json& payload) {
    (void)cmd;
    command_execution_result out;
    auto params = service::load_control_params();
    service::ssh_request req;
    req.host = payload_value_or<std::string>(payload, "host", params.ssh.host);
    req.port = payload_value_or<int>(payload, "port", params.ssh.port);
    req.user = payload_value_or<std::string>(payload, "user", params.ssh.user);
    req.password = payload_value_or<std::string>(payload, "password", params.ssh.password);
    req.timeout_ms = payload_value_or<int>(payload, "timeout_ms", params.ssh.timeout_ms);
    req.command = "poweroff";

    const auto res = service::run_ssh_command(req);
    out.status = res.ok ? command_status::succeeded : command_status::failed;
    out.exit_code = res.exit_status;
    out.result = {
        {"ok", res.ok},
        {"error", res.error},
        {"output", res.output},
        {"exit_status", res.exit_status},
        {"host", req.host},
        {"port", req.port},
        {"user", req.user},
        {"command", req.command},
    };
    return out;
  });

  command_executors_.emplace(command_type::host_probe_get, [this](const command& cmd, const nlohmann::json& payload) {
    (void)cmd;
    (void)payload;
    command_execution_result out;
    out.status = command_status::succeeded;
    out.exit_code = 0;
    out.result = probe_snapshot_to_json(service::get_host_probe_snapshot());
    return out;
  });

  command_executors_.emplace(command_type::monitoring_set, [this](const command& cmd, const nlohmann::json& payload) {
    (void)cmd;
    command_execution_result out;
    if (!payload.contains("enabled") || !payload["enabled"].is_boolean()) {
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = {{"error", "field enabled must be boolean"}};
      return out;
    }

    service::set_host_probe_monitoring_enabled(payload["enabled"].get<bool>());
    out.status = command_status::succeeded;
    out.exit_code = 0;
    out.result = {{"enabled", service::is_host_probe_monitoring_enabled()}};
    return out;
  });

  command_executors_.emplace(command_type::params_get, [this](const command& cmd, const nlohmann::json& payload) {
    (void)cmd;
    (void)payload;
    command_execution_result out;
    out.status = command_status::succeeded;
    out.exit_code = 0;
    out.result = params_to_json(service::load_control_params());
    return out;
  });

  command_executors_.emplace(command_type::params_set, [this](const command& cmd, const nlohmann::json& payload) {
    (void)cmd;
    command_execution_result out;

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
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = {{"error", field_error}};
      return out;
    }

    std::string save_error;
    if (!service::save_control_params(params, save_error)) {
      out.status = command_status::failed;
      out.exit_code = -1;
      out.result = {{"error", save_error}};
      return out;
    }

    out.status = command_status::succeeded;
    out.exit_code = 0;
    out.result = params_to_json(params);
    return out;
  });
}

bool agent_runtime::send_control_message(
    message_type type,
    const std::string& trace_id,
    payload_variant data) {
  auto* ch = wss_channel_.get();
  if (ch == nullptr || !ch->is_running()) {
    log::warn("send message skipped: channel unavailable");
    return false;
  }

  envelope message;
  message.type = type;
  message.protocol = options_.protocol_version;
  message.sent_at_ms = unix_time_ms_now();
  message.trace_id = trace_id.empty() ? make_message_id() : trace_id;
  message.agent_id = options_.agent_id;
  message.data = std::move(data);
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
      heartbeat_payload{unix_time_ms_now(), build_heartbeat_stats()});
}

bool agent_runtime::send_command_ack(
    const std::string& trace_id,
    const std::string& command_id,
    command_status status,
    std::string_view message) {
  return send_control_message(
      message_type::agent_command_ack,
      trace_id,
      command_ack_payload{command_id, status, std::string(message)});
}

channel_callbacks agent_runtime::build_callbacks(const std::string& endpoint) {
  channel_callbacks callbacks;
  callbacks.on_connected = [this, endpoint]() {
    log::info("control channel connected: endpoint={}", endpoint);
    if (!send_register()) {
      log::warn("send register on connected failed: endpoint={}", endpoint);
    }
  };
  callbacks.on_disconnected = [endpoint]() {
    log::warn("control channel disconnected: endpoint={}", endpoint);
  };
  callbacks.on_error = [endpoint](const std::string& err) {
    log::error("control channel error (endpoint={}): {}", endpoint, err);
  };
  callbacks.on_message = [this, endpoint](const envelope& message) {
    log::info(
        "control channel message received (endpoint={}): trace_id={}, type={}",
        endpoint,
        message.trace_id,
        to_string(message.type));
    handle_channel_message(message);
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
  if (!is_supported_protocol_version(message.protocol)) {
    log::error(
        "control channel message ignored: unsupported protocol={}, local_version={}",
        message.protocol,
        current_protocol_version());
    return;
  }

  if (message.type == message_type::server_error) {
    const auto* payload = std::get_if<error_payload>(&message.data);
    if (payload != nullptr) {
      log::error(
          "control plane returned error: code={}, message={}, detail={}",
          payload->code,
          payload->message,
          payload->detail);
    } else {
      log::error("control plane returned malformed error payload");
    }
    return;
  }

  if (message.type != message_type::server_command_dispatch) {
    return;
  }

  const auto* cmd = std::get_if<command>(&message.data);
  if (cmd == nullptr) {
    log::warn("server.command.dispatch payload missing command body");
    return;
  }

  if (!send_command_ack(message.trace_id, cmd->command_id, command_status::acked, "accepted")) {
    log::warn("command ack send failed: command_id={}", cmd->command_id);
    return;
  }

  const bool should_execute = mark_command_seen(cmd->command_id);
  if (!should_execute) {
    log::info("duplicate command ignored: command_id={}", cmd->command_id);
    return;
  }

  const auto checked_at_ms = unix_time_ms_now();
  if (is_command_expired(*cmd, checked_at_ms)) {
    nlohmann::json result = {
        {"ok", false},
        {"error_code", "command_expired"},
        {"message", "command expired before execution"},
        {"expires_at_ms", cmd->expires_at_ms},
        {"checked_at_ms", checked_at_ms},
    };
    if (!send_command_result(
            message.trace_id,
            cmd->command_id,
            command_status::timed_out,
            -1,
            result)) {
      log::warn("send expired command result failed: command_id={}", cmd->command_id);
    }
    log::warn(
        "command expired, skip execution: command_id={}, expires_at_ms={}, checked_at_ms={}",
        cmd->command_id,
        cmd->expires_at_ms,
        checked_at_ms);
    return;
  }

  enqueue_command(message.trace_id, *cmd);
}

void agent_runtime::execute_command(const std::string& trace_id, const command& cmd) {
  nlohmann::json payload = cmd.payload;
  if (!payload.is_object()) {
    payload = nlohmann::json::object();
  }

  const auto it = command_executors_.find(cmd.type);
  if (it == command_executors_.end()) {
    (void)send_command_result(
        trace_id,
        cmd.command_id,
        command_status::failed,
        -1,
        nlohmann::json{{"error", "unsupported command_type"}});
    return;
  }

  const auto execution = it->second(cmd, payload);
  (void)send_command_result(
      trace_id,
      cmd.command_id,
      execution.status,
      execution.exit_code,
      execution.result);
}

bool agent_runtime::send_command_result(
    const std::string& trace_id,
    const std::string& command_id,
    command_status final_status,
    int exit_code,
    const nlohmann::json& result) {
  return send_control_message(
      message_type::agent_command_result,
      trace_id,
      command_result_payload{command_id, final_status, exit_code, result});
}

void agent_runtime::enqueue_command(const std::string& trace_id, const command& cmd) {
  {
    std::lock_guard<std::mutex> lk(queue_mutex_);
    queue_.push_back(queued_command{trace_id, cmd});
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
    try {
      execute_command(item.trace_id, item.cmd);
    } catch (const std::exception& ex) {
      log::error("execute command failed with exception: command_id={}, err={}", item.cmd.command_id, ex.what());
      nlohmann::json result = {
          {"ok", false},
          {"error_code", "internal_error"},
          {"message", "command execution exception"},
      };
      result["detail"] = ex.what();
      if (!send_command_result(item.trace_id, item.cmd.command_id, command_status::failed, -1, result)) {
        log::warn(
            "send command result after exception failed: command_id={}", item.cmd.command_id);
      }
    } catch (...) {
      log::error("execute command failed with unknown exception: command_id={}", item.cmd.command_id);
      nlohmann::json result = {
          {"ok", false},
          {"error_code", "internal_error"},
          {"message", "command execution exception"},
          {"detail", "unknown exception"},
      };
      if (!send_command_result(item.trace_id, item.cmd.command_id, command_status::failed, -1, result)) {
        log::warn(
            "send command result after unknown exception failed: command_id={}", item.cmd.command_id);
      }
    }
  }
}

} // namespace control
