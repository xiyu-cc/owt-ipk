#pragma once

#include "control/runtime_event_dispatcher.h"
#include "control/wss_control_channel.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

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
  agent_runtime();
  ~agent_runtime();

  bool start(const agent_runtime_options& options);
  void stop();
  bool is_running() const noexcept;

  bool send_register();
  bool send_heartbeat();

private:
  struct command_execution_result {
    command_status status = command_status::failed;
    int exit_code = -1;
    nlohmann::json result = nlohmann::json::object();
  };

  using command_executor = std::function<command_execution_result(const command&, const nlohmann::json&)>;

  void install_command_executors();
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
  bool send_command_result(
      const nlohmann::json& request_id,
      const std::string& command_id,
      command_status final_status,
      int exit_code,
      const nlohmann::json& result);
  void enqueue_command(const nlohmann::json& request_id, const command& cmd);
  void execution_loop();

  struct queued_command {
    nlohmann::json request_id = nullptr;
    command cmd;
  };

private:
  std::unique_ptr<i_control_channel> wss_channel_;
  agent_runtime_options options_{};
  std::atomic<bool> running_{false};
  std::mutex seen_commands_mutex_;
  std::unordered_set<std::string> seen_command_ids_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::deque<queued_command> queue_;
  std::map<command_type, command_executor> command_executors_;
  runtime_event_dispatcher event_dispatcher_;
  bool execution_stop_ = false;
  std::thread execution_thread_;
};

} // namespace control
