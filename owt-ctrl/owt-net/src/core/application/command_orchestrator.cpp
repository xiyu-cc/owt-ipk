#include "ctrl/application/command_orchestrator.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace ctrl::application {

namespace {

constexpr int kMinWaitTimeoutMs = 100;
constexpr int kMaxWaitTimeoutMs = 120000;

} // namespace

CommandOrchestrator::CommandOrchestrator(
    ports::ICommandRepository& commands,
    ports::IAgentChannel& channel,
    ParamsService& params,
    ports::IAuditRepository& audits,
    ports::IStatusPublisher& publisher,
    ports::IMetrics& metrics,
    const ports::IClock& clock,
    ports::IIdGenerator& id_generator)
    : commands_(commands),
      channel_(channel),
      params_(params),
      audits_(audits),
      publisher_(publisher),
      metrics_(metrics),
      clock_(clock),
      id_generator_(id_generator) {}

SubmitCommandOutput CommandOrchestrator::submit(const SubmitCommandInput& in) {
  if (in.agent.mac.empty()) {
    throw std::invalid_argument("agent.mac is required");
  }

  const auto now_ms = clock_.now_ms();

  domain::CommandSpec spec;
  spec.command_id = in.command_id.empty() ? id_generator_.next_command_id() : in.command_id;
  spec.trace_id =
      in.trace_id.empty() ? id_generator_.next_trace_id(spec.command_id) : in.trace_id;
  spec.kind = in.kind;
  spec.payload = in.payload;
  spec.timeout_ms = (in.timeout_ms > 0) ? in.timeout_ms : 5000;
  spec.max_retry = std::max(0, in.max_retry);
  spec.expires_at_ms = now_ms + std::max<int64_t>(60000, spec.timeout_ms + 1000LL);

  domain::CommandSnapshot snapshot;
  snapshot.spec = spec;
  snapshot.agent = in.agent;
  snapshot.state = domain::CommandState::Created;
  snapshot.result = nullptr;
  snapshot.retry_count = 0;
  snapshot.next_retry_at_ms = 0;
  snapshot.last_error.clear();
  snapshot.created_at_ms = now_ms;
  snapshot.updated_at_ms = now_ms;

  if (spec.kind == domain::CommandKind::ParamsSet) {
    const auto merged = params_.merge_and_validate(in.agent.mac, spec.payload);
    params_.save(in.agent.mac, merged);
    snapshot.spec.payload = merged;
    if (snapshot.spec.max_retry < 1) {
      snapshot.spec.max_retry = 1;
    }
  }

  std::string dispatch_error;
  if (channel_.is_online(in.agent.mac)) {
    if (channel_.send_command(in.agent.mac, snapshot.spec, dispatch_error)) {
      snapshot.state = domain::CommandState::Dispatched;
      metrics_.record_command_push();
    } else if (snapshot.spec.max_retry > 0) {
      snapshot.state = domain::CommandState::RetryPending;
      snapshot.last_error = dispatch_error;
      snapshot.next_retry_at_ms = now_ms;
      metrics_.record_command_retry(snapshot.spec.command_id, snapshot.retry_count, dispatch_error);
    } else {
      throw std::runtime_error("dispatch command failed: " + dispatch_error);
    }
  } else if (snapshot.spec.max_retry > 0) {
    snapshot.state = domain::CommandState::RetryPending;
    snapshot.last_error = "agent not connected";
    snapshot.next_retry_at_ms = now_ms;
    metrics_.record_command_retry(snapshot.spec.command_id, snapshot.retry_count, snapshot.last_error);
  } else {
    throw std::runtime_error("agent not connected");
  }

  snapshot.updated_at_ms = now_ms;

  std::string error;
  if (!commands_.upsert(snapshot, error)) {
    throw std::runtime_error("persist command failed: " + error);
  }
  const auto event = append_event(
      spec.command_id,
      snapshot.state == domain::CommandState::Dispatched ? "command_push_sent"
                                                        : "command_retry_scheduled",
      snapshot.state,
      nlohmann::json{
          {"trace_id", spec.trace_id},
          {"command_type", domain::to_string(spec.kind)},
          {"error", snapshot.last_error},
      },
      now_ms);
  publisher_.publish_command_event(snapshot, event);
  append_audit(in, spec, now_ms);
  publisher_.publish_snapshot(
      snapshot.state == domain::CommandState::Dispatched ? "command_dispatched" : "command_retry_scheduled",
      in.agent.mac);

  if (!in.wait_result) {
    return to_submit_output(snapshot, false);
  }

  bool wait_timed_out = false;
  auto waited = wait_for_command_result(spec.command_id, in.wait_timeout_ms, wait_timed_out);
  return to_submit_output(waited, wait_timed_out);
}

domain::CommandSnapshot CommandOrchestrator::get(std::string_view command_id) const {
  return load_command_or_throw(command_id);
}

domain::ListPage<domain::CommandSnapshot, domain::CommandListCursor> CommandOrchestrator::list(
    const domain::CommandListFilter& filter) const {
  domain::ListPage<domain::CommandSnapshot, domain::CommandListCursor> page;
  std::string error;
  if (!commands_.list(filter, page, error)) {
    throw std::runtime_error("list commands failed: " + error);
  }
  return page;
}

std::vector<domain::CommandEvent> CommandOrchestrator::events(
    std::string_view command_id,
    int limit) const {
  if (command_id.empty()) {
    throw std::invalid_argument("command_id is required");
  }
  std::vector<domain::CommandEvent> out;
  std::string error;
  if (!commands_.list_events(command_id, limit, out, error)) {
    throw std::runtime_error("list command events failed: " + error);
  }
  return out;
}

int CommandOrchestrator::clamp_wait_timeout(int timeout_ms) {
  return std::clamp(timeout_ms, kMinWaitTimeoutMs, kMaxWaitTimeoutMs);
}

SubmitCommandOutput CommandOrchestrator::to_submit_output(
    const domain::CommandSnapshot& snapshot,
    bool wait_timed_out) {
  SubmitCommandOutput out;
  out.command_id = snapshot.spec.command_id;
  out.trace_id = snapshot.spec.trace_id;
  out.state = snapshot.state;
  out.result = snapshot.result;
  out.terminal = domain::is_terminal(snapshot.state);
  out.wait_timed_out = wait_timed_out;
  out.updated_at_ms = snapshot.updated_at_ms;
  return out;
}

domain::CommandSnapshot CommandOrchestrator::wait_for_command_result(
    std::string_view command_id,
    int wait_timeout_ms,
    bool& wait_timed_out) const {
  wait_timed_out = false;
  const auto safe_timeout = clamp_wait_timeout(wait_timeout_ms > 0 ? wait_timeout_ms : 6000);
  const auto deadline = clock_.now_ms() + static_cast<int64_t>(safe_timeout);

  domain::CommandSnapshot latest;
  for (;;) {
    latest = load_command_or_throw(command_id);
    if (domain::is_terminal(latest.state) || latest.state == domain::CommandState::RetryPending) {
      return latest;
    }

    if (clock_.now_ms() >= deadline) {
      wait_timed_out = true;
      return latest;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }
}

domain::CommandEvent CommandOrchestrator::append_event(
    std::string_view command_id,
    std::string_view event_type,
    domain::CommandState state,
    const nlohmann::json& detail,
    int64_t created_at_ms) {
  domain::CommandEvent event;
  event.command_id = std::string(command_id);
  event.type = std::string(event_type);
  event.state = state;
  event.detail = detail;
  event.created_at_ms = created_at_ms;

  std::string error;
  if (!commands_.append_event(event, error)) {
    throw std::runtime_error("append command event failed: " + error);
  }
  return event;
}

void CommandOrchestrator::append_audit(
    const SubmitCommandInput& in,
    const domain::CommandSpec& spec,
    int64_t created_at_ms) {
  domain::AuditEntry audit;
  audit.actor_type = in.actor_type.empty() ? "system" : in.actor_type;
  audit.actor_id = in.actor_id.empty() ? "unknown" : in.actor_id;
  audit.action = "control_command_push";
  audit.resource_type = "command";
  audit.resource_id = spec.command_id;
  audit.summary = nlohmann::json{
      {"agent_mac", in.agent.mac},
      {"agent_id", in.agent.display_id},
      {"command_id", spec.command_id},
      {"command_type", domain::to_string(spec.kind)},
      {"trace_id", spec.trace_id},
  };
  audit.created_at_ms = created_at_ms;
  std::string error;
  if (!audits_.append(audit, error)) {
    throw std::runtime_error("append audit failed: " + error);
  }
}

domain::CommandSnapshot CommandOrchestrator::load_command_or_throw(std::string_view command_id) const {
  if (command_id.empty()) {
    throw std::invalid_argument("command_id is required");
  }
  domain::CommandSnapshot snapshot;
  std::string error;
  if (!commands_.get(command_id, snapshot, error)) {
    throw std::runtime_error("load command failed: " + error);
  }
  return snapshot;
}

} // namespace ctrl::application
