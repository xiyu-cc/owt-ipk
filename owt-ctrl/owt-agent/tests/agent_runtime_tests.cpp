#include "control/agent_runtime.h"
#include "log.h"

#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class fake_control_channel final : public control::i_control_channel {
public:
  bool start(const control::channel_start_options& options, control::channel_callbacks callbacks) override {
    std::lock_guard<std::mutex> lk(mutex_);
    options_ = options;
    callbacks_ = std::move(callbacks);
    running_ = true;
    return true;
  }

  void stop() override {
    std::lock_guard<std::mutex> lk(mutex_);
    running_ = false;
  }

  bool send(const control::envelope& message) override {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!running_) {
      return false;
    }
    sent_.push_back(message);
    return true;
  }

  bool is_running() const noexcept override {
    std::lock_guard<std::mutex> lk(mutex_);
    return running_;
  }

  void emit_message(const control::envelope& message) {
    control::channel_callbacks callbacks;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      callbacks = callbacks_;
    }
    if (callbacks.on_message) {
      callbacks.on_message(message);
    }
  }

  std::vector<control::envelope> sent_messages() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return sent_;
  }

private:
  mutable std::mutex mutex_;
  bool running_ = false;
  control::channel_start_options options_{};
  control::channel_callbacks callbacks_{};
  std::vector<control::envelope> sent_;
};

bool wait_until(std::function<bool()> predicate, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

void test_ack_running_result_sequence() {
  auto channel = std::make_unique<fake_control_channel>();
  auto* raw_channel = channel.get();

  control::agent_runtime runtime(std::move(channel));
  runtime.register_command_executor(
      control::command_type::host_reboot,
      [](const control::command&, const nlohmann::json&) {
        control::command_execution_result out;
        out.status = control::command_status::succeeded;
        out.exit_code = 0;
        out.result = nlohmann::json{{"ok", true}, {"source", "test"}};
        return out;
      });

  control::agent_runtime_options options;
  options.agent_id = "agent-test";
  options.agent_mac = "AA:00:00:00:00:01";
  options.wss_endpoint = "ws://test/ws/v5/agent";

  require(runtime.start(options), "runtime start failed");

  control::command cmd;
  cmd.command_id = "cmd-1";
  cmd.idempotency_key = "cmd-1";
  cmd.type = control::command_type::host_reboot;
  cmd.issued_at_ms = control::unix_time_ms_now();
  cmd.expires_at_ms = cmd.issued_at_ms + 60000;
  cmd.timeout_ms = 5000;
  cmd.max_retry = 0;
  cmd.payload = nlohmann::json::object();

  control::envelope dispatch;
  dispatch.type = control::message_type::server_command_dispatch;
  dispatch.version = control::current_protocol_version();
  dispatch.id = "req-1";
  dispatch.ts_ms = control::unix_time_ms_now();
  dispatch.target = options.agent_mac;
  dispatch.payload = cmd;

  raw_channel->emit_message(dispatch);

  require(
      wait_until(
          [raw_channel]() {
            return raw_channel->sent_messages().size() >= 3;
          },
          2000),
      "timed out waiting for runtime outbound messages");

  const auto sent = raw_channel->sent_messages();
  require(sent.size() >= 3, "expected at least 3 outbound messages");
  require(sent[0].type == control::message_type::agent_command_ack, "first outbound should be command ack");
  require(sent[1].type == control::message_type::agent_command_ack, "second outbound should be command ack");
  require(sent[2].type == control::message_type::agent_command_result, "third outbound should be command result");

  const auto* ack1 = std::get_if<control::command_ack_payload>(&sent[0].payload);
  require(ack1 != nullptr, "first outbound payload should be command_ack_payload");
  require(ack1->status == control::command_status::acked, "first ack should be ACKED");

  const auto* ack2 = std::get_if<control::command_ack_payload>(&sent[1].payload);
  require(ack2 != nullptr, "second outbound payload should be command_ack_payload");
  require(ack2->status == control::command_status::running, "second ack should be RUNNING");

  const auto* result = std::get_if<control::command_result_payload>(&sent[2].payload);
  require(result != nullptr, "third outbound payload should be command_result_payload");
  require(result->final_status == control::command_status::succeeded, "final status should be SUCCEEDED");

  raw_channel->emit_message(dispatch);
  require(
      wait_until(
          [raw_channel]() {
            return raw_channel->sent_messages().size() >= 4;
          },
          2000),
      "timed out waiting for duplicate command ack");
  const auto after_duplicate = raw_channel->sent_messages();
  require(after_duplicate.size() == 4, "duplicate dispatch should only produce one extra ACKED message");
  const auto* dup_ack = std::get_if<control::command_ack_payload>(&after_duplicate[3].payload);
  require(dup_ack != nullptr, "duplicate outbound payload should be command_ack_payload");
  require(dup_ack->status == control::command_status::acked, "duplicate command should only emit ACKED");

  runtime.stop();
}

} // namespace

int main() {
  try {
    log::init();
    test_ack_running_result_sequence();
    log::shutdown();
    std::cout << "owt-agent runtime tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    log::shutdown();
    std::cerr << "owt-agent runtime tests failed: " << ex.what() << '\n';
    return 1;
  }
}
