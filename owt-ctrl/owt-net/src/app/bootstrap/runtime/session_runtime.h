#pragma once

#include "app/presenter/serializers.h"
#include "app/ws/command_bus_protocol.h"
#include "app/ws/scheduler/event_scheduler.h"
#include "ctrl/application/agent_registry_service.h"
#include "ctrl/domain/types.h"
#include "ctrl/ports/interfaces.h"

#include <drogon/HttpRequest.h>
#include <drogon/WebSocketConnection.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace app::bootstrap::runtime {

struct ConnectionContext {
  std::string session_id;
  std::string actor_id;
};

std::string normalize_actor_id(const drogon::HttpRequestPtr& req, std::string_view fallback);
std::string next_session_id();
const char* to_post_result_string(app::ws::scheduler::PostResult result) noexcept;

class RuntimeMetrics final : public ctrl::ports::IMetrics {
public:
  void record_rate_limited(std::string_view actor_id, int64_t retry_after_ms) override;
  void record_command_push() override;
  void record_command_retry(
      std::string_view command_id,
      int retry_count,
      std::string_view reason) override;
  void record_command_retry_exhausted(
      std::string_view command_id,
      std::string_view reason) override;
  void record_command_terminal_status(
      std::string_view command_id,
      ctrl::domain::CommandState state,
      const nlohmann::json& detail) override;

private:
  std::atomic<uint64_t> rate_limited_total_{0};
  std::atomic<uint64_t> command_push_total_{0};
  std::atomic<uint64_t> command_retry_total_{0};
  std::atomic<uint64_t> command_retry_exhausted_total_{0};
  std::atomic<uint64_t> command_succeeded_total_{0};
  std::atomic<uint64_t> command_failed_total_{0};
  std::atomic<uint64_t> command_timed_out_total_{0};
};

class UiSubscriptionStore {
public:
  enum class Scope {
    All,
    Agent,
  };

  struct Subscription {
    Scope scope = Scope::All;
    std::string agent_mac;
  };

  void subscribe_all(std::string_view session_id);
  void subscribe_agent(std::string_view session_id, std::string_view agent_mac);
  void unsubscribe(std::string_view session_id);
  std::vector<std::pair<std::string, Subscription>> snapshot() const;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Subscription> rows_;
};

class UiSessionRegistry {
public:
  struct Config {
    int queue_limit = 1024;
    int send_timeout_ms = 2000;
  };

  explicit UiSessionRegistry(Config cfg);
  ~UiSessionRegistry();

  void add_session(
      std::string session_id,
      const drogon::WebSocketConnectionPtr& conn,
      std::string actor_id);

  std::string actor_of(std::string_view session_id) const;
  bool enqueue(std::string_view session_id, std::string payload);
  void remove_session(std::string_view session_id);
  void close_all();

private:
  struct Session {
    std::string id;
    std::string actor_id;
    drogon::WebSocketConnectionPtr conn;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::string> queue;
    bool closed = false;
    std::thread writer;
  };

  static void session_writer_loop(const std::shared_ptr<Session>& session);
  static void shutdown_session(const std::shared_ptr<Session>& session, std::string_view reason);
  static void stop_session(const std::shared_ptr<Session>& session);

  Config cfg_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
};

class AgentSessionRegistry {
public:
  struct Session {
    std::string session_id;
    std::string agent_mac;
    std::string agent_id;
    drogon::WebSocketConnectionPtr conn;
  };

  void add_connection(std::string session_id, const drogon::WebSocketConnectionPtr& conn);
  void bind_agent(std::string_view session_id, std::string_view agent_mac, std::string_view agent_id);
  std::string find_agent_for_session(std::string_view session_id) const;
  std::string unbind_session(std::string_view session_id);
  bool is_online(std::string_view agent_mac) const;

  bool send_event_to_agent(
      std::string_view agent_mac,
      std::string event_name,
      const nlohmann::json& payload,
      int64_t now_ms,
      std::string& error);

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Session> by_session_;
  std::unordered_map<std::string, std::string> by_agent_;
};

class DrogonAgentChannel final : public ctrl::ports::IAgentChannel {
public:
  DrogonAgentChannel(const ctrl::ports::IClock& clock, AgentSessionRegistry& sessions);

  bool is_online(std::string_view agent_mac) const override;
  bool send_command(
      std::string_view agent_mac,
      const ctrl::domain::CommandSpec& cmd,
      std::string& error) override;

private:
  const ctrl::ports::IClock& clock_;
  AgentSessionRegistry& sessions_;
};

class BusStatusPublisher final : public ctrl::ports::IStatusPublisher {
public:
  BusStatusPublisher(
      ctrl::application::AgentRegistryService& registry,
      const ctrl::ports::IClock& clock,
      UiSubscriptionStore& subscriptions,
      UiSessionRegistry& ui_sessions,
      app::ws::scheduler::EventScheduler& event_scheduler);

  void push_snapshot_to_session(std::string_view session_id, std::string_view reason);
  void push_agent_to_session(
      std::string_view session_id,
      std::string_view agent_mac,
      std::string_view reason);

  void publish_snapshot(std::string_view reason, std::string_view agent_mac) override;
  void publish_agent(std::string_view reason, std::string_view agent_mac) override;
  void publish_command_event(
      const ctrl::domain::CommandSnapshot& command,
      const ctrl::domain::CommandEvent& event) override;

private:
  ctrl::application::AgentRegistryService& registry_;
  const ctrl::ports::IClock& clock_;
  UiSubscriptionStore& subscriptions_;
  UiSessionRegistry& ui_sessions_;
  app::ws::scheduler::EventScheduler& event_scheduler_;
};

} // namespace app::bootstrap::runtime
