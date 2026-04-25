#pragma once

#include "control/control_protocol.h"

#include <exception>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace control {

class agent_runtime_execution_worker {
public:
  using before_execute_fn = std::function<void(const nlohmann::json&, const command&)>;
  using execute_fn = std::function<void(const nlohmann::json&, const command&)>;
  using exception_fn = std::function<void(const nlohmann::json&, const command&, std::exception_ptr)>;

  agent_runtime_execution_worker() = default;
  ~agent_runtime_execution_worker();

  agent_runtime_execution_worker(const agent_runtime_execution_worker&) = delete;
  agent_runtime_execution_worker& operator=(const agent_runtime_execution_worker&) = delete;

  bool start(before_execute_fn before_execute, execute_fn execute, exception_fn on_exception);
  void stop();
  void enqueue(const nlohmann::json& request_id, const command& cmd);

private:
  struct queued_command {
    nlohmann::json request_id = nullptr;
    command cmd;
  };

  void run();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<queued_command> queue_;
  bool running_ = false;
  bool stop_requested_ = false;
  std::thread worker_;
  before_execute_fn before_execute_;
  execute_fn execute_;
  exception_fn on_exception_;
};

} // namespace control
