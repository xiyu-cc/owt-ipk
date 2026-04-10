#include "control/control_protocol.h"
#include "log.h"
#include "service/auth.h"
#include "service/command_store.h"
#include "service/sensitive_json.h"

#include <boost/beast/http.hpp>

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

namespace http = boost::beast::http;

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void test_protocol_version_support() {
  require(
      std::string(control::current_protocol_version()) == "v1.0-draft",
      "unexpected current protocol version");
  require(control::is_supported_protocol_version("v1.0-draft"), "expected current version to pass");
  require(!control::is_supported_protocol_version("v1.3-experimental"), "expected non-current version to fail");
  require(!control::is_supported_protocol_version("v2.0-draft"), "expected different major version to fail");
  require(!control::is_supported_protocol_version("draft"), "expected malformed version to fail");
}

void test_http_authorization_google_forward_headers() {
  http::request<http::string_body> req{http::verb::get, "/api/v1/control/agents/get", 11};
  require(!service::is_http_request_authorized(req), "missing identity headers should be rejected");

  req.set("X-Forwarded-Proto", "https");
  require(!service::is_http_request_authorized(req), "missing Google identity headers should be rejected");

  req.set("X-Forwarded-Email", "user@gmail.com");
  require(service::is_http_request_authorized(req), "https + forwarded email should pass");
}

void test_command_terminal_status_once() {
  const auto unique_suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / ("owt-net-tests-command-store-" + unique_suffix);
  std::filesystem::create_directories(temp_dir);
  const std::filesystem::path db_path = temp_dir / "owt_net_test.db";

  ::setenv("OWT_CTRL_DB_PATH", db_path.c_str(), 1);

  std::string error;
  require(service::init_command_store(error), "init_command_store failed: " + error);

  service::command_record record;
  record.command_id = "cmd-terminal-once";
  record.agent_id = "agent-test";
  record.idempotency_key = "idem-terminal-once";
  record.command_type = "HOST_PROBE_GET";
  record.status = "DISPATCHED";
  record.payload_json = "{}";
  record.result_json = "";
  record.created_at_ms = 1000;
  record.updated_at_ms = 1000;
  require(service::upsert_command(record, error), "upsert_command failed: " + error);

  bool applied = false;
  error.clear();
  require(
      service::update_command_terminal_status_once(
          record.command_id, "SUCCEEDED", "{\"ok\":true}", 1100, applied, error),
      "first terminal update failed: " + error);
  require(applied, "first terminal update should apply");

  error.clear();
  require(
      service::update_command_terminal_status_once(
          record.command_id, "FAILED", "{\"ok\":false}", 1200, applied, error),
      "second terminal update should return success for duplicate terminal result");
  require(!applied, "second terminal update should be ignored");

  service::command_record stored;
  error.clear();
  require(service::get_command(record.command_id, stored, error), "get_command failed: " + error);
  require(stored.status == "SUCCEEDED", "stored status should keep first terminal result");
  require(stored.result_json == "{\"ok\":true}", "stored result should keep first terminal payload");

  service::command_record inflight;
  inflight.command_id = "cmd-recover";
  inflight.agent_id = "agent-test";
  inflight.idempotency_key = "idem-recover";
  inflight.command_type = "HOST_REBOOT";
  inflight.status = "ACKED";
  inflight.payload_json = "{}";
  inflight.result_json = "";
  inflight.created_at_ms = 2000;
  inflight.updated_at_ms = 2000;
  error.clear();
  require(service::upsert_command(inflight, error), "upsert inflight command failed: " + error);

  int recovered_count = 0;
  error.clear();
  require(
      service::recover_inflight_commands(3000, recovered_count, error),
      "recover_inflight_commands failed: " + error);
  require(recovered_count == 1, "expected one inflight command to be recovered");

  error.clear();
  require(service::get_command(inflight.command_id, stored, error), "get recovered command failed: " + error);
  require(stored.status == "TIMED_OUT", "recovered command should become TIMED_OUT");

  service::shutdown_command_store();
  ::unsetenv("OWT_CTRL_DB_PATH");
  std::filesystem::remove_all(temp_dir);
}

void test_agent_params_store_roundtrip() {
  const auto unique_suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::filesystem::path temp_dir =
      std::filesystem::temp_directory_path() / ("owt-net-tests-agent-params-" + unique_suffix);
  std::filesystem::create_directories(temp_dir);
  const std::filesystem::path db_path = temp_dir / "owt_net_test.db";

  ::setenv("OWT_CTRL_DB_PATH", db_path.c_str(), 1);
  std::string error;
  require(service::init_command_store(error), "init_command_store failed: " + error);

  const std::string payload = R"({"ssh":{"host":"192.168.1.2","password":"secret"}})";
  error.clear();
  require(
      service::upsert_agent_params("agent-test", payload, 12345, error),
      "upsert_agent_params failed: " + error);

  service::agent_params_record row;
  error.clear();
  require(service::get_agent_params("agent-test", row, error), "get_agent_params failed: " + error);
  require(row.agent_id == "agent-test", "agent_id mismatch");
  require(row.params_json == payload, "params_json mismatch");
  require(row.updated_at_ms == 12345, "updated_at_ms mismatch");

  service::shutdown_command_store();
  ::unsetenv("OWT_CTRL_DB_PATH");
  std::filesystem::remove_all(temp_dir);
}

void test_sensitive_redaction() {
  const std::string source =
      R"({"ssh":{"host":"192.168.1.10","password":"my-secret"},"token":"abc"})";
  const auto redacted = service::redact_sensitive_json_text(source);
  require(redacted.find("my-secret") == std::string::npos, "password must be redacted");
  require(redacted.find("\"password\":\"***\"") != std::string::npos, "password placeholder missing");
  require(redacted.find("\"token\":\"***\"") != std::string::npos, "token placeholder missing");

  const std::string plain_text = "password=my-secret token=abc";
  const auto redacted_text = service::redact_sensitive_json_text(plain_text);
  require(redacted_text.find("my-secret") == std::string::npos, "plain text password must be redacted");
  require(redacted_text.find("token=***") != std::string::npos, "plain text token placeholder missing");
}

} // namespace

int main() {
  try {
    log::init();
    test_protocol_version_support();
    test_http_authorization_google_forward_headers();
    test_command_terminal_status_once();
    test_agent_params_store_roundtrip();
    test_sensitive_redaction();
    log::shutdown();
    std::cout << "owt-net tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "owt-net tests failed: " << ex.what() << '\n';
    log::shutdown();
    return 1;
  }
}
