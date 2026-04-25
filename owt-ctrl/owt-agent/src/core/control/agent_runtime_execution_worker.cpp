#include "control/agent_runtime_execution_worker.h"

#include <utility>

namespace control {

agent_runtime_execution_worker::~agent_runtime_execution_worker() {
  stop();
}

bool agent_runtime_execution_worker::start(
    before_execute_fn before_execute,
    execute_fn execute,
    exception_fn on_exception) {
  if (!execute) {
    return false;
  }

  std::lock_guard<std::mutex> lk(mutex_);
  if (running_) {
    return true;
  }

  before_execute_ = std::move(before_execute);
  execute_ = std::move(execute);
  on_exception_ = std::move(on_exception);
  queue_.clear();
  stop_requested_ = false;
  running_ = true;
  worker_ = std::thread([this]() { run(); });
  return true;
}

void agent_runtime_execution_worker::stop() {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!running_) {
      return;
    }
    stop_requested_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }

  std::lock_guard<std::mutex> lk(mutex_);
  running_ = false;
  stop_requested_ = false;
  queue_.clear();
}

void agent_runtime_execution_worker::enqueue(const nlohmann::json& request_id, const command& cmd) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!running_ || stop_requested_) {
      return;
    }
    queue_.push_back(queued_command{request_id, cmd});
  }
  cv_.notify_one();
}

void agent_runtime_execution_worker::run() {
  while (true) {
    queued_command item;
    {
      std::unique_lock<std::mutex> lk(mutex_);
      cv_.wait(lk, [this]() { return stop_requested_ || !queue_.empty(); });
      if (stop_requested_ && queue_.empty()) {
        return;
      }
      item = std::move(queue_.front());
      queue_.pop_front();
    }

    try {
      if (before_execute_) {
        before_execute_(item.request_id, item.cmd);
      }
      execute_(item.request_id, item.cmd);
    } catch (...) {
      if (on_exception_) {
        on_exception_(item.request_id, item.cmd, std::current_exception());
      }
    }
  }
}

} // namespace control
