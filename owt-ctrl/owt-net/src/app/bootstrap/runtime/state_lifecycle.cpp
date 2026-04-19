#include "internal.h"

namespace app::bootstrap::runtime_internal {

RuntimeImplState::RuntimeImplState(const Config& in_config)
    : config(in_config),
      store(config.storage.db_path),
      agent_channel(clock),
      registry_service(store, clock),
      status_publisher(registry_service, clock, subscriptions),
      params_service(store, clock),
      command_orchestrator(
          store,
          agent_channel,
          params_service,
          store,
          status_publisher,
          metrics,
          clock,
          id_generator),
      agent_message_service(
          store,
          registry_service,
          status_publisher,
          metrics,
          clock),
      retry_service(store, agent_channel, status_publisher, metrics, clock),
      audit_query_service(store),
      control_ws_use_cases(registry_service, agent_message_service) {}

std::string RuntimeImplState::next_trace_id() {
  const auto now = clock.now_ms();
  const auto seq = trace_seq.fetch_add(1, std::memory_order_relaxed);
  return "trc-" + std::to_string(now) + "-" + std::to_string(seq);
}

void RuntimeImplState::send_agent_envelope(
    ws_deal::ws_hub_api& hub,
    std::string_view session_id,
    std::string_view type,
    std::string_view trace_id,
    std::string_view agent_id,
    const nlohmann::json& payload) {
  if (session_id.empty()) {
    return;
  }
  ws::AgentEnvelope envelope;
  envelope.type = std::string(type);
  envelope.meta.version = std::string(kProtocolVersion);
  envelope.meta.trace_id = trace_id.empty() ? next_trace_id() : std::string(trace_id);
  envelope.meta.ts_ms = clock.now_ms();
  envelope.meta.agent_id = std::string(agent_id);
  envelope.payload = payload;
  hub.publish_to_session(
      std::string(session_id),
      true,
      ws::encode_agent_envelope(envelope));
}

} // namespace app::bootstrap::runtime_internal
