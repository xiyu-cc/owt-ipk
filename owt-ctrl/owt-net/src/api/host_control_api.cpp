#include "api/host_poweroff_api.h"
#include "api/host_reboot_api.h"

#include "api/api_common.h"
#include "service/ssh_executor.h"

#include <nlohmann/json.hpp>

#include <string>

namespace api {

namespace {

http_deal::http::message_generator run_host_command(request_t& req, const std::string& command) {
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(body_as_string(req));
  } catch (const std::exception&) {
    return reply_error(req, http_deal::http::status::bad_request, "invalid json body");
  }

  service::ssh_request ssh_req;
  ssh_req.host = body.value("host", "");
  ssh_req.port = body.value("port", 22);
  ssh_req.user = body.value("user", body.value("username", ""));
  ssh_req.password = body.value("password", "");
  ssh_req.timeout_ms = body.value("timeout_ms", 5000);
  ssh_req.command = command;

  if (ssh_req.host.empty() || ssh_req.user.empty() || ssh_req.password.empty()) {
    return reply_error(req, http_deal::http::status::bad_request, "host/user/password is required");
  }

  const auto res = service::run_ssh_command(ssh_req);
  if (!res.ok) {
    return reply_error(
        req,
        http_deal::http::status::bad_gateway,
        "ssh command failed: " + (!res.error.empty() ? res.error : res.output),
        res.exit_status);
  }

  nlohmann::json data = {
      {"host", ssh_req.host},
      {"port", ssh_req.port},
      {"user", ssh_req.user},
      {"command", ssh_req.command},
      {"exit_status", res.exit_status},
      {"output", res.output},
  };
  return reply_ok(req, data);
}

} // namespace

http_deal::http::message_generator host_reboot_api::operator()(request_t& req) {
  return run_host_command(req, "reboot");
}

http_deal::http::message_generator host_poweroff_api::operator()(request_t& req) {
  return run_host_command(req, "poweroff");
}

} // namespace api
