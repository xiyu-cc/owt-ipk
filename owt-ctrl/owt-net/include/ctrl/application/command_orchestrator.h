#pragma once

#include "ctrl/domain/types.h"
#include "ctrl/ports/interfaces.h"
#include "ctrl/application/params_service.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace ctrl::application {

struct SubmitCommandInput {
  domain::AgentRef agent;
  domain::CommandKind kind = domain::CommandKind::HostProbeGet;
  nlohmann::json payload = nlohmann::json::object();
  int timeout_ms = 5000;
  int max_retry = 1;
  bool wait_result = true;
  int wait_timeout_ms = 6000;
  std::string command_id;
  std::string trace_id;
  std::string actor_type;
  std::string actor_id;
};

struct SubmitCommandOutput {
  std::string command_id;
  std::string trace_id;
  domain::CommandState state = domain::CommandState::Created;
  nlohmann::json result = nullptr;
  bool terminal = false;
  bool wait_timed_out = false;
  int64_t updated_at_ms = 0;
};

class CommandOrchestrator {
public:
  CommandOrchestrator(
      ports::ICommandRepository& commands,
      ports::IAgentChannel& channel,
      ParamsService& params,
      ports::IAuditRepository& audits,
      ports::IStatusPublisher& publisher,
      ports::IMetrics& metrics,
      const ports::IClock& clock,
      ports::IIdGenerator& id_generator);

  SubmitCommandOutput submit(const SubmitCommandInput& in);
  domain::CommandSnapshot get(std::string_view command_id) const;
  domain::ListPage<domain::CommandSnapshot, domain::CommandListCursor> list(
      const domain::CommandListFilter& filter) const;
  std::vector<domain::CommandEvent> events(std::string_view command_id, int limit) const;

private:
  static int clamp_wait_timeout(int timeout_ms);
  static SubmitCommandOutput to_submit_output(
      const domain::CommandSnapshot& snapshot,
      bool wait_timed_out);

  domain::CommandSnapshot wait_for_command_result(
      std::string_view command_id,
      int wait_timeout_ms,
      bool& wait_timed_out) const;
  domain::CommandEvent append_event(
      std::string_view command_id,
      std::string_view event_type,
      domain::CommandState state,
      const nlohmann::json& detail,
      int64_t created_at_ms);
  void append_audit(const SubmitCommandInput& in, const domain::CommandSpec& spec, int64_t created_at_ms);
  domain::CommandSnapshot load_command_or_throw(std::string_view command_id) const;

  ports::ICommandRepository& commands_;
  ports::IAgentChannel& channel_;
  ParamsService& params_;
  ports::IAuditRepository& audits_;
  ports::IStatusPublisher& publisher_;
  ports::IMetrics& metrics_;
  const ports::IClock& clock_;
  ports::IIdGenerator& id_generator_;
};

} // namespace ctrl::application
