#pragma once

#include "control/grpc_control_channel.h"
#include "control/wss_control_channel.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace control {

struct agent_runtime_options {
  std::string agent_id = "agent-local";
  std::string protocol_version = "v1.0-draft";
  std::string management_token;

  bool enable_wss = true;
  bool enable_grpc = true;
  bool prefer_wss = true;

  std::string wss_endpoint = "wss://127.0.0.1:9527/ws/control";
  std::string grpc_endpoint = "127.0.0.1:50051";
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
  i_control_channel* select_primary_channel();
  i_control_channel* channel_for_type(channel_type type);
  channel_callbacks build_callbacks(channel_type type);
  bool start_channel(i_control_channel& channel, const std::string& endpoint);
  void handle_channel_message(channel_type type, const envelope& message);
  void persist_command_push(channel_type channel, const command& cmd);
  void persist_command_ack(channel_type channel, const std::string& command_id);
  void execute_command(channel_type channel, const std::string& trace_id, const command& cmd);
  bool send_command_result(
      channel_type channel,
      const std::string& trace_id,
      const std::string& command_id,
      command_status final_status,
      int exit_code,
      const std::string& result_json);
  void persist_command_result(
      channel_type channel,
      const std::string& command_id,
      command_status final_status,
      int exit_code,
      const std::string& result_json);
  void enqueue_command(channel_type channel, const std::string& trace_id, const command& cmd);
  void execution_loop();

  struct queued_command {
    channel_type channel = channel_type::wss;
    std::string trace_id;
    command cmd;
  };

private:
  std::unique_ptr<i_control_channel> wss_channel_;
  std::unique_ptr<i_control_channel> grpc_channel_;
  i_control_channel* primary_channel_ = nullptr;
  agent_runtime_options options_{};
  std::atomic<bool> running_{false};
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::deque<queued_command> queue_;
  bool execution_stop_ = false;
  std::thread execution_thread_;
};

} // namespace control
