#pragma once

#include "control/wss_control_channel.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

namespace control {

struct agent_runtime_options {
  std::string agent_id = "agent-local";
  std::string protocol_version = "v1.0-draft";

  std::string wss_endpoint = "wss://owt.wzhex.com/ws/control";
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
  channel_callbacks build_callbacks(const std::string& endpoint);
  bool start_channel(i_control_channel& channel, const std::string& endpoint);
  bool mark_command_seen(const std::string& command_id);
  void handle_channel_message(const envelope& message);
  void execute_command(const std::string& trace_id, const command& cmd);
  bool send_command_result(
      const std::string& trace_id,
      const std::string& command_id,
      command_status final_status,
      int exit_code,
      const std::string& result_json);
  void enqueue_command(const std::string& trace_id, const command& cmd);
  void execution_loop();

  struct queued_command {
    std::string trace_id;
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
  bool execution_stop_ = false;
  std::thread execution_thread_;
};

} // namespace control
