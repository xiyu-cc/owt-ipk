#include "api/monitoring_get_api.h"
#include "api/monitoring_set_api.h"

#include "api/api_common.h"
#include "service/host_probe_agent.h"

#include <nlohmann/json.hpp>

namespace api {

http_deal::http::message_generator monitoring_get_api::operator()(request_t& req) {
  return reply_ok(req, {{"enabled", service::is_host_probe_monitoring_enabled()}});
}

http_deal::http::message_generator monitoring_set_api::operator()(request_t& req) {
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(body_as_string(req));
  } catch (const std::exception&) {
    return reply_error(req, http_deal::http::status::bad_request, "invalid json body");
  }

  if (!body.is_object()) {
    return reply_error(req, http_deal::http::status::bad_request, "json body must be object");
  }
  if (!body.contains("enabled") || !body["enabled"].is_boolean()) {
    return reply_error(req, http_deal::http::status::bad_request, "field enabled must be boolean");
  }

  const bool enabled = body["enabled"].get<bool>();
  service::set_host_probe_monitoring_enabled(enabled);
  return reply_ok(req, {{"enabled", service::is_host_probe_monitoring_enabled()}});
}

} // namespace api
