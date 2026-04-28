#include "app/bootstrap/runtime/ws_envelope_validator.h"

#include "owt/protocol/v5/contract.h"

namespace app::bootstrap::runtime {

namespace {

void send_bad_envelope(
    WsPeer peer,
    const ctrl::ports::IClock& clock,
    const drogon::WebSocketConnectionPtr& conn,
    std::string_view parse_error) {
  if (peer == WsPeer::Ui) {
    conn->send(
        ws::bus_error(
            "unknown",
            nullptr,
            clock.now_ms(),
            owt::protocol::v5::error_code::kBadEnvelope,
            std::string(parse_error)),
        drogon::WebSocketMessageType::Text);
    return;
  }

  conn->send(
      ws::bus_error(
          owt::protocol::v5::agent::kErrorServerError,
          nullptr,
          clock.now_ms(),
          owt::protocol::v5::error_code::kBadEnvelope,
          std::string(parse_error)),
      drogon::WebSocketMessageType::Text);
}

void send_unsupported_version(
    const ctrl::ports::IClock& clock,
    const drogon::WebSocketConnectionPtr& conn,
    const ws::BusEnvelope& envelope) {
  conn->send(
      ws::bus_error(
          envelope.name,
          envelope.id,
          clock.now_ms(),
          owt::protocol::v5::error_code::kUnsupportedVersion,
          "unsupported protocol version",
          nlohmann::json{{"expected", std::string(owt::protocol::v5::kProtocol)}, {"got", envelope.version}}),
      drogon::WebSocketMessageType::Text);
}

void send_bad_kind(
    WsPeer peer,
    const ctrl::ports::IClock& clock,
    const drogon::WebSocketConnectionPtr& conn,
    const ws::BusEnvelope& envelope) {
  const auto message = peer == WsPeer::Ui
      ? std::string("ui message kind must be action")
      : std::string("agent message kind must be action");
  conn->send(
      ws::bus_error(
          envelope.name,
          envelope.id,
          clock.now_ms(),
          owt::protocol::v5::error_code::kBadKind,
          std::move(message)),
      drogon::WebSocketMessageType::Text);
}

} // namespace

bool parse_and_validate_action_envelope(
    WsPeer peer,
    std::string_view text,
    const ctrl::ports::IClock& clock,
    const drogon::WebSocketConnectionPtr& conn,
    ws::BusEnvelope& out) {
  std::string parse_error;
  if (!ws::parse_bus_envelope(text, out, parse_error)) {
    send_bad_envelope(peer, clock, conn, parse_error);
    return false;
  }

  if (out.version != owt::protocol::v5::kProtocol) {
    send_unsupported_version(clock, conn, out);
    return false;
  }

  if (out.kind != owt::protocol::v5::kind::kAction) {
    send_bad_kind(peer, clock, conn, out);
    return false;
  }
  if (!out.target.empty()) {
    send_bad_envelope(peer, clock, conn, "target is not allowed for action");
    return false;
  }

  return true;
}

} // namespace app::bootstrap::runtime
