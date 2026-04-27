#pragma once

#include "control/agent_command_executor_registry.h"
#include "control/agent_runtime_execution_worker.h"
#include "control/agent_runtime_heartbeat_builder.h"
#include "control/agent_runtime_message_router.h"
#include "control/i_control_channel.h"
#include "control/runtime_event_dispatcher.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace control {

struct agent_runtime_options {
  std::string agent_id = "agent-local";
  std::string agent_mac = "";
  std::string protocol_version = "v5";

  std::string wss_endpoint = "wss://owt.wzhex.com/ws/v5/agent";
  int ws_event_workers = 2;
  int ws_event_queue_capacity = 4096;
};

class agent_runtime {
public:
  using now_ms_provider = std::function<int64_t()>;

  agent_runtime();
  explicit agent_runtime(std::unique_ptr<i_control_channel> channel);
  agent_runtime(
      std::unique_ptr<i_control_channel> channel,
      std::shared_ptr<agent_command_executor_registry> executor_registry,
      agent_runtime_heartbeat_builder heartbeat_builder);
  agent_runtime(
      std::unique_ptr<i_control_channel> channel,
      std::shared_ptr<agent_command_executor_registry> executor_registry,
      agent_runtime_heartbeat_builder heartbeat_builder,
      now_ms_provider now_ms_fn,
      std::size_t seen_command_cache_max_size,
      int64_t seen_command_cache_ttl_ms);
  ~agent_runtime();

  bool start(const agent_runtime_options& options);
  void stop();
  bool is_running() const noexcept;

  bool send_register();
  bool send_heartbeat();

  void register_command_executor(command_type type, command_executor executor);

private:
  static constexpr std::size_t kDefaultSeenCommandCacheMaxSize = 50'000;
  static constexpr int64_t kDefaultSeenCommandCacheTtlMs = 86'400'000;

  bool send_command_ack(
      const nlohmann::json& request_id,
      const std::string& command_id,
      command_status status,
      std::string_view message);
  bool send_control_message(
      message_type type,
      const nlohmann::json& request_id,
      payload_variant data);
  channel_callbacks build_callbacks(const std::string& endpoint);
  bool start_channel(i_control_channel& channel, const std::string& endpoint);
  bool mark_command_seen(const std::string& command_id);
  void handle_channel_message(const envelope& message);
  void execute_command(const nlohmann::json& request_id, const command& cmd);
  void on_execute_exception(
      const nlohmann::json& request_id,
      const command& cmd,
      std::exception_ptr exception_ptr);
  bool send_command_result(
      const nlohmann::json& request_id,
      const std::string& command_id,
      command_status final_status,
      int exit_code,
      const nlohmann::json& result);
  void enqueue_command(const nlohmann::json& request_id, const command& cmd);

private:
  std::unique_ptr<i_control_channel> control_channel_;
  std::shared_ptr<agent_command_executor_registry> executor_registry_;
  agent_runtime_heartbeat_builder heartbeat_builder_;
  agent_runtime_message_router message_router_;

  agent_runtime_options options_{};
  std::atomic<bool> running_{false};
  std::mutex seen_commands_mutex_;
  std::unordered_map<std::string, int64_t> seen_command_ids_;
  std::deque<std::pair<std::string, int64_t>> seen_command_order_;
  now_ms_provider now_ms_fn_{};
  std::size_t seen_command_cache_max_size_ = kDefaultSeenCommandCacheMaxSize;
  int64_t seen_command_cache_ttl_ms_ = kDefaultSeenCommandCacheTtlMs;

  runtime_event_dispatcher event_dispatcher_;
  agent_runtime_execution_worker execution_worker_;
};

} // namespace control
