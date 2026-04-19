#pragma once

#include "app/config.h"
#include "app/presenter/serializers.h"
#include "app/ws/agent_protocol.h"
#include "app/ws/jsonrpc_protocol.h"
#include "ctrl/adapters/control_ws_use_cases.h"
#include "ctrl/infrastructure/sqlite_store.h"
#include "ctrl/domain/types.h"
#include "ctrl/ports/defaults.h"
#include "ctrl/ports/interfaces.h"
#include "ctrl/application/agent_message_service.h"
#include "ctrl/application/agent_registry_service.h"
#include "ctrl/application/audit_query_service.h"
#include "ctrl/application/command_orchestrator.h"
#include "ctrl/application/params_service.h"
#include "ctrl/application/retry_service.h"
#include "detail/ws_deal/handler.h"
#include "detail/ws_deal/handler_dispatcher.h"
#include "detail/ws_deal/hub_api.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace app::bootstrap::runtime_internal {

constexpr std::string_view kProtocolVersion = "v3";
constexpr std::string_view kWsAgentRoute = "/ws/v3/agent";
constexpr std::string_view kWsUiRoute = "/ws/v3/ui";

bool parse_int(const nlohmann::json& value, int& out);
bool parse_int64(const nlohmann::json& value, int64_t& out);

class AppMetrics final : public ctrl::ports::IMetrics {
public:
  void record_http_request() override;
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
  std::atomic<uint64_t> http_requests_total_{0};
  std::atomic<uint64_t> rate_limited_total_{0};
  std::atomic<uint64_t> command_push_total_{0};
  std::atomic<uint64_t> command_retry_total_{0};
  std::atomic<uint64_t> command_retry_exhausted_total_{0};
  std::atomic<uint64_t> command_succeeded_total_{0};
  std::atomic<uint64_t> command_failed_total_{0};
  std::atomic<uint64_t> command_timed_out_total_{0};
};

class WsAgentChannel final : public ctrl::ports::IAgentChannel {
public:
  explicit WsAgentChannel(const ctrl::ports::IClock& clock);

  void set_hub(ws_deal::ws_hub_api* hub);
  void bind_session(
      std::string_view agent_mac,
      std::string_view agent_id,
      std::string_view session_id);
  void unbind_session(std::string_view session_id);
  std::string find_agent_for_session(std::string_view session_id) const;

  bool is_online(std::string_view agent_mac) const override;
  bool send_command(
      std::string_view agent_mac,
      const ctrl::domain::CommandSpec& cmd,
      std::string& error) override;

private:
  const ctrl::ports::IClock& clock_;
  mutable std::mutex mutex_;
  ws_deal::ws_hub_api* hub_ = nullptr;
  std::unordered_map<std::string, std::string> agent_sessions_;
  std::unordered_map<std::string, std::string> session_agents_;
  std::unordered_map<std::string, std::string> agent_display_ids_;
  std::atomic<uint64_t> message_seq_{0};
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

class WsUiPublisher final : public ctrl::ports::IStatusPublisher {
public:
  WsUiPublisher(
      ctrl::application::AgentRegistryService& registry,
      const ctrl::ports::IClock& clock,
      UiSubscriptionStore& subscriptions);

  void set_hub(ws_deal::ws_hub_api* hub);
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
  void send_notify(
      std::string_view session_id,
      std::string_view method,
      const nlohmann::json& resource,
      const nlohmann::json& extra_meta);

  ctrl::application::AgentRegistryService& registry_;
  const ctrl::ports::IClock& clock_;
  UiSubscriptionStore& subscriptions_;

  std::mutex mutex_;
  ws_deal::ws_hub_api* hub_ = nullptr;
};

struct RuntimeImplState {
  Config config;
  ctrl::ports::SystemClock clock;
  ctrl::ports::DefaultIdGenerator id_generator;
  AppMetrics metrics;
  ctrl::infrastructure::SqliteStore store;
  UiSubscriptionStore subscriptions;
  WsAgentChannel agent_channel;
  ctrl::application::AgentRegistryService registry_service;
  WsUiPublisher status_publisher;
  ctrl::application::ParamsService params_service;
  ctrl::application::CommandOrchestrator command_orchestrator;
  ctrl::application::AgentMessageService agent_message_service;
  ctrl::application::RetryService retry_service;
  ctrl::application::AuditQueryService audit_query_service;
  ctrl::adapters::ControlWsUseCases control_ws_use_cases;

  std::shared_ptr<ws_deal::handler_dispatcher> ws_dispatcher;

  std::mutex worker_mutex;
  std::condition_variable worker_cv;
  bool worker_stop = false;
  std::thread retry_worker;
  std::thread cleanup_worker;

  std::atomic<uint64_t> trace_seq{0};

  explicit RuntimeImplState(const Config& in_config);

  std::string next_trace_id();
  void send_agent_envelope(
      ws_deal::ws_hub_api& hub,
      std::string_view session_id,
      std::string_view type,
      std::string_view trace_id,
      std::string_view agent_id,
      const nlohmann::json& payload);
  void start_workers();
  void stop_workers();
};

class AgentWsHandler final : public ws_deal::handler {
public:
  explicit AgentWsHandler(RuntimeImplState& state);

  void on_join(ws_deal::ws_hub_api& hub, std::string_view session_id) override;
  void on_leave(ws_deal::ws_hub_api& hub, std::string_view session_id) override;
  void on_message(ws_deal::ws_hub_api& hub, ws_deal::inbound_message message) override;

private:
  RuntimeImplState& state_;
};

class UiRpcWsHandler final : public ws_deal::handler {
public:
  explicit UiRpcWsHandler(RuntimeImplState& state);

  void on_join(ws_deal::ws_hub_api& hub, std::string_view session_id) override;
  void on_leave(ws_deal::ws_hub_api& hub, std::string_view session_id) override;
  void on_message(ws_deal::ws_hub_api& hub, ws_deal::inbound_message message) override;

private:
  RuntimeImplState& state_;
};

} // namespace app::bootstrap::runtime_internal
