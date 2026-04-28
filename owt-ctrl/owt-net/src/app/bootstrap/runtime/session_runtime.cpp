#include "app/bootstrap/runtime/session_runtime.h"

#include "app/runtime_log.h"
#include "owt/protocol/v5/contract.h"

#include <algorithm>
#include <chrono>

namespace app::bootstrap::runtime {

std::string normalize_actor_id(const drogon::HttpRequestPtr& req, std::string_view fallback) {
  if (req == nullptr) {
    return std::string(fallback);
  }
  const auto email = req->getHeader("x-forwarded-email");
  if (!email.empty()) {
    return email;
  }
  const auto user = req->getHeader("x-forwarded-user");
  if (!user.empty()) {
    return user;
  }
  return std::string(fallback);
}

std::string next_session_id() {
  static std::atomic<uint64_t> seq{0};
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const auto id = seq.fetch_add(1, std::memory_order_relaxed);
  return "sess-" + std::to_string(now) + "-" + std::to_string(id);
}

const char* to_post_result_string(app::ws::scheduler::PostResult result) noexcept {
  using app::ws::scheduler::PostResult;
  switch (result) {
    case PostResult::Accepted:
      return "accepted";
    case PostResult::DroppedLowPriority:
      return "dropped_low_priority";
    case PostResult::RejectedHighPriority:
      return "rejected_high_priority";
    case PostResult::Stopped:
      return "stopped";
  }
  return "unknown";
}

void RuntimeMetrics::record_rate_limited(std::string_view actor_id, int64_t retry_after_ms) {
  (void)actor_id;
  (void)retry_after_ms;
  ++rate_limited_total_;
}

void RuntimeMetrics::record_command_push() {
  ++command_push_total_;
}

void RuntimeMetrics::record_command_retry(
    std::string_view command_id,
    int retry_count,
    std::string_view reason) {
  (void)command_id;
  (void)retry_count;
  (void)reason;
  ++command_retry_total_;
}

void RuntimeMetrics::record_command_retry_exhausted(
    std::string_view command_id,
    std::string_view reason) {
  (void)command_id;
  (void)reason;
  ++command_retry_exhausted_total_;
}

void RuntimeMetrics::record_command_terminal_status(
    std::string_view command_id,
    ctrl::domain::CommandState state,
    const nlohmann::json& detail) {
  (void)command_id;
  (void)detail;
  switch (state) {
    case ctrl::domain::CommandState::Succeeded:
      ++command_succeeded_total_;
      break;
    case ctrl::domain::CommandState::Failed:
      ++command_failed_total_;
      break;
    case ctrl::domain::CommandState::TimedOut:
      ++command_timed_out_total_;
      break;
    default:
      break;
  }
}

void UiSubscriptionStore::subscribe_all(std::string_view session_id) {
  if (session_id.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  rows_[std::string(session_id)] = Subscription{Scope::All, ""};
}

void UiSubscriptionStore::subscribe_agent(std::string_view session_id, std::string_view agent_mac) {
  if (session_id.empty() || agent_mac.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  rows_[std::string(session_id)] = Subscription{Scope::Agent, std::string(agent_mac)};
}

void UiSubscriptionStore::unsubscribe(std::string_view session_id) {
  if (session_id.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  rows_.erase(std::string(session_id));
}

std::vector<std::pair<std::string, UiSubscriptionStore::Subscription>> UiSubscriptionStore::snapshot() const {
  std::lock_guard<std::mutex> lk(mutex_);
  std::vector<std::pair<std::string, Subscription>> out;
  out.reserve(rows_.size());
  for (const auto& [session_id, sub] : rows_) {
    out.emplace_back(session_id, sub);
  }
  return out;
}

UiSessionRegistry::UiSessionRegistry(Config cfg) : cfg_(cfg) {}

UiSessionRegistry::~UiSessionRegistry() {
  close_all();
}

void UiSessionRegistry::add_session(
    std::string session_id,
    const drogon::WebSocketConnectionPtr& conn,
    std::string actor_id) {
  auto session = std::make_shared<Session>();
  session->id = std::move(session_id);
  session->actor_id = std::move(actor_id);
  session->conn = conn;
  session->writer = std::thread([session] { session_writer_loop(session); });

  std::lock_guard<std::mutex> lk(mutex_);
  sessions_[session->id] = session;
}

std::string UiSessionRegistry::actor_of(std::string_view session_id) const {
  std::lock_guard<std::mutex> lk(mutex_);
  const auto it = sessions_.find(std::string(session_id));
  if (it == sessions_.end()) {
    return {};
  }
  return it->second->actor_id;
}

bool UiSessionRegistry::enqueue(std::string_view session_id, std::string payload) {
  std::shared_ptr<Session> session;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = sessions_.find(std::string(session_id));
    if (it == sessions_.end()) {
      return false;
    }
    session = it->second;
  }

  bool queue_overflow = false;
  {
    std::lock_guard<std::mutex> lk(session->mutex);
    if (session->closed) {
      return false;
    }
    if (static_cast<int>(session->queue.size()) >= cfg_.queue_limit) {
      queue_overflow = true;
    } else {
      session->queue.push_back(std::move(payload));
    }
  }
  if (queue_overflow) {
    shutdown_session(session, "ui queue overflow");
    return false;
  }
  session->cv.notify_one();
  return true;
}

void UiSessionRegistry::remove_session(std::string_view session_id) {
  std::shared_ptr<Session> session;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto it = sessions_.find(std::string(session_id));
    if (it == sessions_.end()) {
      return;
    }
    session = it->second;
    sessions_.erase(it);
  }

  stop_session(session);
}

void UiSessionRegistry::close_all() {
  std::vector<std::shared_ptr<Session>> rows;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    rows.reserve(sessions_.size());
    for (auto& [id, session] : sessions_) {
      (void)id;
      rows.push_back(session);
    }
    sessions_.clear();
  }
  for (auto& session : rows) {
    stop_session(session);
  }
}

void UiSessionRegistry::session_writer_loop(const std::shared_ptr<Session>& session) {
  while (true) {
    std::string payload;
    {
      std::unique_lock<std::mutex> lk(session->mutex);
      session->cv.wait(lk, [&session] { return session->closed || !session->queue.empty(); });
      if (session->closed && session->queue.empty()) {
        return;
      }
      payload = std::move(session->queue.front());
      session->queue.pop_front();
    }
    session->cv.notify_all();

    if (!session->conn || !session->conn->connected()) {
      continue;
    }
    session->conn->send(std::move(payload), drogon::WebSocketMessageType::Text);
  }
}

void UiSessionRegistry::shutdown_session(const std::shared_ptr<Session>& session, std::string_view reason) {
  if (!session || !session->conn) {
    return;
  }
  if (!session->conn->connected()) {
    return;
  }
  log::warn("disconnect slow ui session {}: {}", session->id, reason);
  session->conn->shutdown(drogon::CloseCode::kViolation, std::string(reason));
}

void UiSessionRegistry::stop_session(const std::shared_ptr<Session>& session) {
  if (!session) {
    return;
  }
  {
    std::lock_guard<std::mutex> lk(session->mutex);
    session->closed = true;
  }
  session->cv.notify_all();
  if (session->writer.joinable()) {
    session->writer.join();
  }
}

void AgentSessionRegistry::add_connection(std::string session_id, const drogon::WebSocketConnectionPtr& conn) {
  if (session_id.empty() || !conn) {
    return;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  auto& row = by_session_[session_id];
  row.session_id = std::move(session_id);
  row.conn = conn;
}

void AgentSessionRegistry::bind_agent(std::string_view session_id, std::string_view agent_mac, std::string_view agent_id) {
  if (session_id.empty() || agent_mac.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lk(mutex_);
  auto sit = by_session_.find(std::string(session_id));
  if (sit == by_session_.end()) {
    return;
  }

  if (!sit->second.agent_mac.empty()) {
    by_agent_.erase(sit->second.agent_mac);
  }

  auto old = by_agent_.find(std::string(agent_mac));
  if (old != by_agent_.end()) {
    auto old_sit = by_session_.find(old->second);
    if (old_sit != by_session_.end()) {
      old_sit->second.agent_mac.clear();
      old_sit->second.agent_id.clear();
    }
  }

  sit->second.agent_mac = std::string(agent_mac);
  sit->second.agent_id = agent_id.empty() ? std::string(agent_mac) : std::string(agent_id);
  by_agent_[sit->second.agent_mac] = sit->first;
}

std::string AgentSessionRegistry::find_agent_for_session(std::string_view session_id) const {
  if (session_id.empty()) {
    return {};
  }
  std::lock_guard<std::mutex> lk(mutex_);
  const auto it = by_session_.find(std::string(session_id));
  if (it == by_session_.end()) {
    return {};
  }
  return it->second.agent_mac;
}

std::string AgentSessionRegistry::unbind_session(std::string_view session_id) {
  if (session_id.empty()) {
    return {};
  }
  std::lock_guard<std::mutex> lk(mutex_);
  const auto it = by_session_.find(std::string(session_id));
  if (it == by_session_.end()) {
    return {};
  }
  std::string agent_mac = it->second.agent_mac;
  if (!agent_mac.empty()) {
    by_agent_.erase(agent_mac);
  }
  by_session_.erase(it);
  return agent_mac;
}

bool AgentSessionRegistry::is_online(std::string_view agent_mac) const {
  if (agent_mac.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lk(mutex_);
  const auto ait = by_agent_.find(std::string(agent_mac));
  if (ait == by_agent_.end()) {
    return false;
  }
  const auto sit = by_session_.find(ait->second);
  if (sit == by_session_.end() || !sit->second.conn) {
    return false;
  }
  return sit->second.conn->connected();
}

bool AgentSessionRegistry::send_event_to_agent(
    std::string_view agent_mac,
    std::string event_name,
    const nlohmann::json& payload,
    int64_t now_ms,
    std::string& error) {
  std::shared_ptr<Session> target;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    const auto ait = by_agent_.find(std::string(agent_mac));
    if (ait == by_agent_.end()) {
      error = "agent not connected";
      return false;
    }
    const auto sit = by_session_.find(ait->second);
    if (sit == by_session_.end() || !sit->second.conn) {
      error = "agent session not found";
      return false;
    }
    target = std::make_shared<Session>(sit->second);
  }

  if (!target->conn || !target->conn->connected()) {
    error = "agent connection closed";
    return false;
  }

  const auto text = ws::bus_event(event_name, now_ms, payload, agent_mac);
  target->conn->send(text, drogon::WebSocketMessageType::Text);
  error.clear();
  return true;
}

DrogonAgentChannel::DrogonAgentChannel(const ctrl::ports::IClock& clock, AgentSessionRegistry& sessions)
    : clock_(clock), sessions_(sessions) {}

bool DrogonAgentChannel::is_online(std::string_view agent_mac) const {
  return sessions_.is_online(agent_mac);
}

bool DrogonAgentChannel::send_command(
    std::string_view agent_mac,
    const ctrl::domain::CommandSpec& cmd,
    std::string& error) {
  const auto now_ms = clock_.now_ms();
  const nlohmann::json payload = {
      {"command",
       {
           {"command_id", cmd.command_id},
           {"command_type", ctrl::domain::to_string(cmd.kind)},
           {"issued_at_ms", now_ms},
           {"expires_at_ms", cmd.expires_at_ms},
           {"timeout_ms", cmd.timeout_ms},
           {"max_retry", cmd.max_retry},
           {"payload", cmd.payload},
       }},
  };
  return sessions_.send_event_to_agent(
      agent_mac,
      std::string(owt::protocol::v5::agent::kEventCommandDispatch),
      payload,
      now_ms,
      error);
}

BusStatusPublisher::BusStatusPublisher(
    ctrl::application::AgentRegistryService& registry,
    const ctrl::ports::IClock& clock,
    UiSubscriptionStore& subscriptions,
    UiSessionRegistry& ui_sessions,
    app::ws::scheduler::EventScheduler& event_scheduler)
    : registry_(registry),
      clock_(clock),
      subscriptions_(subscriptions),
      ui_sessions_(ui_sessions),
      event_scheduler_(event_scheduler) {}

std::string BusStatusPublisher::build_snapshot_message(std::string_view reason) const {
  auto agents = registry_.list_agents(true);
  size_t online_count = 0;
  nlohmann::json rows = nlohmann::json::array();
  for (const auto& state : agents) {
    if (state.online) {
      ++online_count;
    }
    rows.push_back(presenter::to_agent_json(state));
  }
  const auto payload = nlohmann::json{
      {"reason", std::string(reason)},
      {"resource",
       {
           {"agents", std::move(rows)},
           {"online_count", online_count},
           {"total_count", agents.size()},
       }},
  };
  return ws::bus_event(
      owt::protocol::v5::ui::kEventAgentSnapshot,
      clock_.now_ms(),
      payload);
}

std::string BusStatusPublisher::build_agent_message(
    std::string_view reason,
    std::string_view agent_mac) const {
  ctrl::domain::AgentState state;
  nlohmann::json resource = {
      {"agent_mac", std::string(agent_mac)},
      {"agent_id", std::string(agent_mac)},
      {"online", false},
      {"stats", nlohmann::json::object()},
  };
  if (registry_.get_agent(agent_mac, state)) {
    resource = presenter::to_agent_json(state);
  }

  const auto payload = nlohmann::json{
      {"reason", std::string(reason)},
      {"resource", std::move(resource)},
  };

  return ws::bus_event(
      owt::protocol::v5::ui::kEventAgentUpdate,
      clock_.now_ms(),
      payload,
      agent_mac);
}

void BusStatusPublisher::push_snapshot_to_session(std::string_view session_id, std::string_view reason) {
  if (session_id.empty()) {
    return;
  }
  const auto message = build_snapshot_message(reason);
  (void)ui_sessions_.enqueue(session_id, message);
}

void BusStatusPublisher::push_agent_to_session(
    std::string_view session_id,
    std::string_view agent_mac,
    std::string_view reason) {
  if (session_id.empty() || agent_mac.empty()) {
    return;
  }
  const auto message = build_agent_message(reason, agent_mac);
  (void)ui_sessions_.enqueue(session_id, message);
}

void BusStatusPublisher::publish_snapshot(std::string_view reason, std::string_view agent_mac) {
  const auto rows = subscriptions_.snapshot();
  bool has_snapshot_subscribers = false;
  bool has_agent_subscribers = false;
  for (const auto& row : rows) {
    if (row.second.scope == UiSubscriptionStore::Scope::All) {
      has_snapshot_subscribers = true;
      continue;
    }
    if (!agent_mac.empty() && row.second.agent_mac == agent_mac) {
      has_agent_subscribers = true;
    }
  }

  std::shared_ptr<const std::string> snapshot_message;
  if (has_snapshot_subscribers) {
    snapshot_message = std::make_shared<const std::string>(build_snapshot_message(reason));
  }

  std::shared_ptr<const std::string> agent_message;
  if (has_agent_subscribers && !agent_mac.empty()) {
    agent_message = std::make_shared<const std::string>(build_agent_message(reason, agent_mac));
  }

  for (const auto& row : rows) {
    const auto session_id = row.first;
    const auto sub = row.second;
    const auto result = event_scheduler_.post(
        session_id,
        app::ws::scheduler::EventPriority::Low,
        [this, session_id, sub, agent_mac = std::string(agent_mac), snapshot_message, agent_message] {
          if (sub.scope == UiSubscriptionStore::Scope::All && snapshot_message) {
            (void)ui_sessions_.enqueue(session_id, *snapshot_message);
            return;
          }
          if (!agent_mac.empty() && sub.agent_mac == agent_mac && agent_message) {
            (void)ui_sessions_.enqueue(session_id, *agent_message);
          }
        });
    if (result == app::ws::scheduler::PostResult::DroppedLowPriority) {
      log::warn(
          "drop low-priority snapshot event: session_id={}, reason={}",
          session_id,
          reason);
    } else if (result != app::ws::scheduler::PostResult::Accepted) {
      log::warn(
          "snapshot event enqueue failed: session_id={}, result={}",
          session_id,
          to_post_result_string(result));
    }
  }
}

void BusStatusPublisher::publish_agent(std::string_view reason, std::string_view agent_mac) {
  if (agent_mac.empty()) {
    return;
  }
  const auto rows = subscriptions_.snapshot();
  for (const auto& row : rows) {
    const auto session_id = row.first;
    const auto sub = row.second;
    const auto result = event_scheduler_.post(
        session_id,
        app::ws::scheduler::EventPriority::High,
        [this,
         session_id,
         sub,
         reason = std::string(reason),
         agent_mac = std::string(agent_mac)] {
          if (sub.scope == UiSubscriptionStore::Scope::All || sub.agent_mac == agent_mac) {
            push_agent_to_session(session_id, agent_mac, reason);
          }
        });
    if (result != app::ws::scheduler::PostResult::Accepted) {
      log::warn(
          "agent update enqueue failed: session_id={}, result={}, agent_mac={}",
          session_id,
          to_post_result_string(result),
          agent_mac);
    }
  }
}

void BusStatusPublisher::publish_command_event(
    const ctrl::domain::CommandSnapshot& command,
    const ctrl::domain::CommandEvent& event) {
  const auto rows = subscriptions_.snapshot();
  const auto resource = presenter::to_command_event_notification(command, event);
  for (const auto& row : rows) {
    const auto session_id = row.first;
    const auto sub = row.second;
    if (sub.scope != UiSubscriptionStore::Scope::All && sub.agent_mac != command.agent.mac) {
      continue;
    }
    const auto result = event_scheduler_.post(
        session_id,
        app::ws::scheduler::EventPriority::High,
        [this, session_id, resource] {
          const auto payload = nlohmann::json{
              {"reason", std::string(owt::protocol::v5::ui::kEventCommandEvent)},
              {"resource", resource},
          };
          const auto text = ws::bus_event(
              owt::protocol::v5::ui::kEventCommandEvent,
              clock_.now_ms(),
              payload);
          (void)ui_sessions_.enqueue(session_id, text);
        });
    if (result != app::ws::scheduler::PostResult::Accepted) {
      log::warn(
          "command event enqueue failed: session_id={}, result={}, command_id={}",
          session_id,
          to_post_result_string(result),
          command.spec.command_id);
    }
  }
}

} // namespace app::bootstrap::runtime
