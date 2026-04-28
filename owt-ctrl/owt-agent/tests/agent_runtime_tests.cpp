#include "control/agent_runtime.h"
#include "control/control_json_codec.h"
#include "control/wss_control_channel.h"
#include "config.h"
#include "log.h"

#include <drogon/WebSocketController.h>
#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
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

class reconnect_ws_probe {
public:
  static void reset() {
    std::lock_guard<std::mutex> lk(mutex_);
    messages_.clear();
    connection_.reset();
    connections_opened_ = 0;
    connections_closed_ = 0;
  }

  static void on_connection_open(const drogon::WebSocketConnectionPtr& conn) {
    {
      std::lock_guard<std::mutex> lk(mutex_);
      connection_ = conn;
      ++connections_opened_;
    }
    cv_.notify_all();
  }

  static void on_connection_closed(const drogon::WebSocketConnectionPtr& conn) {
    {
      std::lock_guard<std::mutex> lk(mutex_);
      if (connection_ && connection_.get() == conn.get()) {
        connection_.reset();
      }
      ++connections_closed_;
    }
    cv_.notify_all();
  }

  static void on_message(std::string&& text, const drogon::WebSocketMessageType& type) {
    if (type != drogon::WebSocketMessageType::Text) {
      return;
    }
    {
      std::lock_guard<std::mutex> lk(mutex_);
      messages_.push_back(std::move(text));
    }
    cv_.notify_all();
  }

  static bool close_active_connection() {
    drogon::WebSocketConnectionPtr conn;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      conn = connection_;
    }
    if (!conn || !conn->connected()) {
      return false;
    }
    conn->shutdown();
    return true;
  }

  static std::vector<std::string> messages_snapshot() {
    std::lock_guard<std::mutex> lk(mutex_);
    return messages_;
  }

  static size_t message_count() {
    std::lock_guard<std::mutex> lk(mutex_);
    return messages_.size();
  }

  static bool wait_for_connections_opened(int min_count, int timeout_ms) {
    std::unique_lock<std::mutex> lk(mutex_);
    return cv_.wait_for(
        lk,
        std::chrono::milliseconds(timeout_ms),
        [min_count]() { return connections_opened_ >= min_count; });
  }

private:
  static std::mutex mutex_;
  static std::condition_variable cv_;
  static std::vector<std::string> messages_;
  static drogon::WebSocketConnectionPtr connection_;
  static int connections_opened_;
  static int connections_closed_;
};

std::mutex reconnect_ws_probe::mutex_;
std::condition_variable reconnect_ws_probe::cv_;
std::vector<std::string> reconnect_ws_probe::messages_;
drogon::WebSocketConnectionPtr reconnect_ws_probe::connection_;
int reconnect_ws_probe::connections_opened_ = 0;
int reconnect_ws_probe::connections_closed_ = 0;

class reconnect_ws_controller final : public drogon::WebSocketController<reconnect_ws_controller> {
public:
  void handleNewMessage(
      const drogon::WebSocketConnectionPtr&,
      std::string&& message,
      const drogon::WebSocketMessageType& type) override {
    reconnect_ws_probe::on_message(std::move(message), type);
  }

  void handleNewConnection(
      const drogon::HttpRequestPtr&,
      const drogon::WebSocketConnectionPtr& conn) override {
    reconnect_ws_probe::on_connection_open(conn);
  }

  void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override {
    reconnect_ws_probe::on_connection_closed(conn);
  }

  WS_PATH_LIST_BEGIN
  WS_PATH_ADD("/ws-test/v5/agent", drogon::Get);
  WS_PATH_LIST_END
};

class scoped_drogon_ws_server {
public:
  explicit scoped_drogon_ws_server(int port) {
    drogon::app().setLogLevel(trantor::Logger::kWarn);
    drogon::app().setThreadNum(1);
    drogon::app().addListener("127.0.0.1", static_cast<uint16_t>(port));
    thread_ = std::thread([]() { drogon::app().run(); });
  }

  ~scoped_drogon_ws_server() {
    drogon::app().quit();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

private:
  std::thread thread_;
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

int pick_free_tcp_port() {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return 38567;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return 38567;
  }

  socklen_t len = sizeof(addr);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    ::close(fd);
    return 38567;
  }

  const int port = static_cast<int>(ntohs(addr.sin_port));
  ::close(fd);
  return (port > 0) ? port : 38567;
}

control::envelope make_dispatch_envelope(
    const std::string& request_id,
    const std::string& agent_mac,
    const std::string& command_id,
    int64_t now_ms) {
  control::command cmd;
  cmd.command_id = command_id;
  cmd.type = control::command_type::host_reboot;
  cmd.issued_at_ms = now_ms;
  cmd.expires_at_ms = now_ms + 60000;
  cmd.timeout_ms = 5000;
  cmd.max_retry = 0;
  cmd.payload = nlohmann::json::object();

  control::envelope dispatch;
  dispatch.type = control::message_type::server_command_dispatch;
  dispatch.version = control::current_protocol_version();
  dispatch.id = request_id;
  dispatch.ts_ms = now_ms;
  dispatch.target = agent_mac;
  dispatch.payload = cmd;
  return dispatch;
}

control::envelope make_register_envelope(
    const std::string& request_id,
    const std::string& agent_id,
    const std::string& agent_mac,
    int64_t now_ms) {
  control::envelope out;
  out.type = control::message_type::agent_register;
  out.version = control::current_protocol_version();
  out.id = request_id;
  out.ts_ms = now_ms;
  out.payload = control::register_payload{
      agent_mac,
      agent_id,
      "default",
      "0.2.0",
      {"wol_wake", "host_reboot", "host_poweroff", "monitoring_set", "params_set"}};
  return out;
}

control::envelope make_ack_envelope(
    const std::string& request_id,
    const std::string& agent_mac,
    const std::string& command_id,
    int64_t now_ms) {
  control::envelope out;
  out.type = control::message_type::agent_command_ack;
  out.version = control::current_protocol_version();
  out.id = request_id;
  out.ts_ms = now_ms;
  out.payload = control::command_ack_payload{
      agent_mac,
      command_id,
      control::command_status::acked,
      "accepted"};
  return out;
}

control::envelope make_result_envelope(
    const std::string& request_id,
    const std::string& agent_mac,
    const std::string& command_id,
    int64_t now_ms) {
  control::envelope out;
  out.type = control::message_type::agent_command_result;
  out.version = control::current_protocol_version();
  out.id = request_id;
  out.ts_ms = now_ms;
  out.payload = control::command_result_payload{
      agent_mac,
      command_id,
      control::command_status::succeeded,
      0,
      nlohmann::json{{"ok", true}}};
  return out;
}

control::envelope make_heartbeat_envelope(
    const std::string& request_id,
    const std::string& agent_mac,
    int64_t now_ms) {
  control::envelope out;
  out.type = control::message_type::agent_heartbeat;
  out.version = control::current_protocol_version();
  out.id = request_id;
  out.ts_ms = now_ms;
  out.payload = control::heartbeat_payload{
      agent_mac,
      now_ms,
      nlohmann::json{{"cpu", 5}}};
  return out;
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
  require(ack1->agent_mac == options.agent_mac, "first ack should carry agent_mac");
  require(ack1->status == control::command_status::acked, "first ack should be ACKED");
  require(sent[0].target.empty(), "first ack should not carry target");

  const auto* ack2 = std::get_if<control::command_ack_payload>(&sent[1].payload);
  require(ack2 != nullptr, "second outbound payload should be command_ack_payload");
  require(ack2->agent_mac == options.agent_mac, "second ack should carry agent_mac");
  require(ack2->status == control::command_status::running, "second ack should be RUNNING");
  require(sent[1].target.empty(), "second ack should not carry target");

  const auto* result = std::get_if<control::command_result_payload>(&sent[2].payload);
  require(result != nullptr, "third outbound payload should be command_result_payload");
  require(result->agent_mac == options.agent_mac, "result should carry agent_mac");
  require(result->final_status == control::command_status::succeeded, "final status should be SUCCEEDED");
  require(sent[2].target.empty(), "result should not carry target");

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
  require(dup_ack->agent_mac == options.agent_mac, "duplicate ack should carry agent_mac");
  require(dup_ack->status == control::command_status::acked, "duplicate command should only emit ACKED");
  require(after_duplicate[3].target.empty(), "duplicate ack should not carry target");

  runtime.stop();
}

void test_seen_command_cache_capacity() {
  int64_t fake_now_ms = 20000;
  auto channel = std::make_unique<fake_control_channel>();
  auto* raw_channel = channel.get();

  control::agent_runtime runtime(
      std::move(channel),
      {},
      control::agent_runtime_heartbeat_builder{},
      [&fake_now_ms]() { return fake_now_ms; },
      2,
      60000);
  runtime.register_command_executor(
      control::command_type::host_reboot,
      [](const control::command&, const nlohmann::json&) {
        control::command_execution_result out;
        out.status = control::command_status::succeeded;
        out.exit_code = 0;
        out.result = nlohmann::json{{"ok", true}};
        return out;
      });

  control::agent_runtime_options options;
  options.agent_id = "agent-capacity";
  options.agent_mac = "AA:00:00:00:00:11";
  options.wss_endpoint = "ws://test/ws/v5/agent";
  options.ws_event_workers = 1;

  require(runtime.start(options), "runtime start failed (capacity)");

  raw_channel->emit_message(make_dispatch_envelope("req-c-1", options.agent_mac, "cmd-cap-1", fake_now_ms));
  require(wait_until([raw_channel]() { return raw_channel->sent_messages().size() >= 3; }, 2000), "capacity step1");

  raw_channel->emit_message(make_dispatch_envelope("req-c-2", options.agent_mac, "cmd-cap-1", fake_now_ms));
  require(wait_until([raw_channel]() { return raw_channel->sent_messages().size() >= 4; }, 2000), "capacity step2");

  raw_channel->emit_message(make_dispatch_envelope("req-c-3", options.agent_mac, "cmd-cap-2", fake_now_ms));
  require(wait_until([raw_channel]() { return raw_channel->sent_messages().size() >= 7; }, 2000), "capacity step3");

  raw_channel->emit_message(make_dispatch_envelope("req-c-4", options.agent_mac, "cmd-cap-3", fake_now_ms));
  require(wait_until([raw_channel]() { return raw_channel->sent_messages().size() >= 10; }, 2000), "capacity step4");

  raw_channel->emit_message(make_dispatch_envelope("req-c-5", options.agent_mac, "cmd-cap-1", fake_now_ms));
  require(wait_until([raw_channel]() { return raw_channel->sent_messages().size() >= 13; }, 2000), "capacity step5");

  runtime.stop();
}

void test_seen_command_cache_ttl() {
  int64_t fake_now_ms = 30000;
  auto channel = std::make_unique<fake_control_channel>();
  auto* raw_channel = channel.get();

  control::agent_runtime runtime(
      std::move(channel),
      {},
      control::agent_runtime_heartbeat_builder{},
      [&fake_now_ms]() { return fake_now_ms; },
      10,
      100);
  runtime.register_command_executor(
      control::command_type::host_reboot,
      [](const control::command&, const nlohmann::json&) {
        control::command_execution_result out;
        out.status = control::command_status::succeeded;
        out.exit_code = 0;
        out.result = nlohmann::json{{"ok", true}};
        return out;
      });

  control::agent_runtime_options options;
  options.agent_id = "agent-ttl";
  options.agent_mac = "AA:00:00:00:00:12";
  options.wss_endpoint = "ws://test/ws/v5/agent";
  options.ws_event_workers = 1;

  require(runtime.start(options), "runtime start failed (ttl)");

  raw_channel->emit_message(make_dispatch_envelope("req-t-1", options.agent_mac, "cmd-ttl-1", fake_now_ms));
  require(wait_until([raw_channel]() { return raw_channel->sent_messages().size() >= 3; }, 2000), "ttl step1");

  raw_channel->emit_message(make_dispatch_envelope("req-t-2", options.agent_mac, "cmd-ttl-1", fake_now_ms));
  require(wait_until([raw_channel]() { return raw_channel->sent_messages().size() >= 4; }, 2000), "ttl step2");

  fake_now_ms += 150;
  raw_channel->emit_message(make_dispatch_envelope("req-t-3", options.agent_mac, "cmd-ttl-1", fake_now_ms));
  require(wait_until([raw_channel]() { return raw_channel->sent_messages().size() >= 7; }, 2000), "ttl step3");

  runtime.stop();
}

void test_start_rejects_unsupported_protocol_version() {
  auto channel = std::make_unique<fake_control_channel>();
  control::agent_runtime runtime(std::move(channel));

  control::agent_runtime_options options;
  options.agent_id = "agent-version-check";
  options.agent_mac = "AA:00:00:00:00:21";
  options.wss_endpoint = "ws://test/ws/v5/agent";
  options.protocol_version = "v4";

  require(!runtime.start(options), "runtime should reject unsupported protocol version");
  require(!runtime.is_running(), "runtime should stay stopped for unsupported protocol version");
}

void test_dispatch_target_mismatch_is_ignored() {
  auto channel = std::make_unique<fake_control_channel>();
  auto* raw_channel = channel.get();

  control::agent_runtime runtime(std::move(channel));
  runtime.register_command_executor(
      control::command_type::host_reboot,
      [](const control::command&, const nlohmann::json&) {
        control::command_execution_result out;
        out.status = control::command_status::succeeded;
        out.exit_code = 0;
        out.result = nlohmann::json{{"ok", true}};
        return out;
      });

  control::agent_runtime_options options;
  options.agent_id = "agent-target-check";
  options.agent_mac = "AA:00:00:00:00:22";
  options.wss_endpoint = "ws://test/ws/v5/agent";
  options.ws_event_workers = 1;

  require(runtime.start(options), "runtime start failed (target mismatch)");

  raw_channel->emit_message(make_dispatch_envelope("req-target-mismatch", "AA:00:00:00:FF:FF", "cmd-target-1", 33000));

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  require(
      raw_channel->sent_messages().empty(),
      "target mismatch dispatch should be ignored without ACK/result");

  runtime.stop();
}

void test_config_parser_rejects_trailing_numeric_garbage() {
  const auto unique = std::to_string(control::unix_time_ms_now());
  const auto path = std::filesystem::path("/tmp") / ("owt-agent-config-" + unique + ".ini");

  {
    std::ofstream out(path);
    out << "[agent]\n";
    out << "heartbeat_interval_ms = 6000x\n";
    out << "status_collect_interval_ms = 1200ms\n";
    out << "ws_event_workers = 4foo\n";
    out << "ws_event_queue_capacity = 8192bar\n";
  }

  const auto cfg = owt_agent::load_config(path.string());
  std::filesystem::remove(path);

  require(cfg.agent.heartbeat_interval_ms == 5000, "invalid heartbeat should keep default");
  require(cfg.agent.status_collect_interval_ms == 1000, "invalid collect interval should keep default");
  require(cfg.agent.ws_event_workers == 2, "invalid workers should keep default");
  require(cfg.agent.ws_event_queue_capacity == 4096, "invalid queue capacity should keep default");
}

void test_wss_reconnect_reliable_outbound_queue() {
  const int kPort = pick_free_tcp_port();
  reconnect_ws_probe::reset();
  scoped_drogon_ws_server server(kPort);

  control::wss_control_channel channel;
  std::atomic<int> connected_count{0};
  std::atomic<int> disconnected_count{0};

  control::channel_callbacks callbacks;
  callbacks.on_connected = [&connected_count]() { connected_count.fetch_add(1, std::memory_order_relaxed); };
  callbacks.on_disconnected = [&disconnected_count]() {
    disconnected_count.fetch_add(1, std::memory_order_relaxed);
  };

  control::channel_start_options options;
  options.agent_id = "agent-reconnect";
  options.endpoint = "ws://127.0.0.1:" + std::to_string(kPort) + "/ws-test/v5/agent";
  options.protocol_version = control::current_protocol_version();

  require(channel.start(options, std::move(callbacks)), "wss channel start should succeed");
  require(
      wait_until([&connected_count]() { return connected_count.load(std::memory_order_relaxed) >= 1; }, 5000),
      "wss channel should connect");
  require(reconnect_ws_probe::wait_for_connections_opened(1, 5000), "server should observe initial connection");

  require(reconnect_ws_probe::close_active_connection(), "server should close active connection");
  require(
      wait_until(
          [&disconnected_count]() { return disconnected_count.load(std::memory_order_relaxed) >= 1; },
          5000),
      "channel should observe disconnect");

  const auto now_ms = control::unix_time_ms_now();
  require(
      channel.send(make_register_envelope("req-reg-reconnect", "agent-reconnect", "AA:00:00:00:90:01", now_ms)),
      "register should be queued while disconnected");
  require(
      channel.send(make_ack_envelope("req-ack-reconnect", "AA:00:00:00:90:01", "cmd-reconnect", now_ms + 1)),
      "ack should be queued while disconnected");
  require(
      channel.send(make_result_envelope("req-res-reconnect", "AA:00:00:00:90:01", "cmd-reconnect", now_ms + 2)),
      "result should be queued while disconnected");
  require(
      !channel.send(make_heartbeat_envelope("req-hb-reconnect", "AA:00:00:00:90:01", now_ms + 3)),
      "heartbeat should be dropped while disconnected");

  require(
      wait_until([&connected_count]() { return connected_count.load(std::memory_order_relaxed) >= 2; }, 10000),
      "channel should reconnect");
  require(reconnect_ws_probe::wait_for_connections_opened(2, 10000), "server should observe reconnected session");
  require(
      wait_until([]() { return reconnect_ws_probe::message_count() >= 3; }, 5000),
      "server should receive queued critical messages");

  bool has_register = false;
  bool has_ack = false;
  bool has_result = false;
  bool has_heartbeat = false;
  for (const auto& raw : reconnect_ws_probe::messages_snapshot()) {
    control::envelope decoded;
    std::string error;
    if (!control::decode_envelope_json(raw, decoded, error)) {
      continue;
    }
    if (decoded.type == control::message_type::agent_register) {
      has_register = true;
    } else if (decoded.type == control::message_type::agent_command_ack) {
      has_ack = true;
    } else if (decoded.type == control::message_type::agent_command_result) {
      has_result = true;
    } else if (decoded.type == control::message_type::agent_heartbeat) {
      has_heartbeat = true;
    }
  }

  require(has_register, "reconnected queue should flush register");
  require(has_ack, "reconnected queue should flush command ack");
  require(has_result, "reconnected queue should flush command result");
  require(!has_heartbeat, "heartbeat should not be replayed after disconnect");

  channel.stop();
}

} // namespace

int main() {
  try {
    log::init();
    test_ack_running_result_sequence();
    test_seen_command_cache_capacity();
    test_seen_command_cache_ttl();
    test_start_rejects_unsupported_protocol_version();
    test_dispatch_target_mismatch_is_ignored();
    test_config_parser_rejects_trailing_numeric_garbage();
    test_wss_reconnect_reliable_outbound_queue();
    log::shutdown();
    std::cout << "owt-agent runtime tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    log::shutdown();
    std::cerr << "owt-agent runtime tests failed: " << ex.what() << '\n';
    return 1;
  }
}
