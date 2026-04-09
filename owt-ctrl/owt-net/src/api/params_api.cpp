#include "api/params_get_api.h"
#include "api/params_set_api.h"

#include "api/api_common.h"
#include "service/params_store.h"

#include <nlohmann/json.hpp>

#include <string>

namespace api {

namespace {

nlohmann::json params_to_json(const service::control_params& params) {
  return {
      {"wol",
       {
           {"mac", params.wol.mac},
           {"broadcast", params.wol.broadcast},
           {"port", params.wol.port},
       }},
      {"ssh",
       {
           {"host", params.ssh.host},
           {"port", params.ssh.port},
           {"user", params.ssh.user},
           {"password", params.ssh.password},
           {"timeout_ms", params.ssh.timeout_ms},
       }},
  };
}

bool update_int_field(
    const nlohmann::json& j,
    const char* key,
    int min_value,
    int max_value,
    int& target,
    std::string& err) {
  if (!j.contains(key)) {
    return true;
  }
  if (!j[key].is_number_integer()) {
    err = std::string("field ") + key + " must be integer";
    return false;
  }
  const int value = j[key].get<int>();
  if (value < min_value || value > max_value) {
    err = std::string("field ") + key + " out of range";
    return false;
  }
  target = value;
  return true;
}

bool update_string_field(
    const nlohmann::json& j,
    const char* key,
    std::string& target,
    std::string& err) {
  if (!j.contains(key)) {
    return true;
  }
  if (!j[key].is_string()) {
    err = std::string("field ") + key + " must be string";
    return false;
  }
  target = j[key].get<std::string>();
  return true;
}

} // namespace

http_deal::http::message_generator params_get_api::operator()(request_t& req) {
  const auto params = service::load_control_params();
  return reply_ok(req, params_to_json(params));
}

http_deal::http::message_generator params_set_api::operator()(request_t& req) {
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(body_as_string(req));
  } catch (const std::exception&) {
    return reply_error(req, http_deal::http::status::bad_request, "invalid json body");
  }

  if (!body.is_object()) {
    return reply_error(req, http_deal::http::status::bad_request, "json body must be object");
  }

  auto params = service::load_control_params();

  if (body.contains("wol")) {
    if (!body["wol"].is_object()) {
      return reply_error(req, http_deal::http::status::bad_request, "field wol must be object");
    }
    const auto& wol = body["wol"];
    std::string err;
    if (!update_string_field(wol, "mac", params.wol.mac, err) ||
        !update_string_field(wol, "broadcast", params.wol.broadcast, err) ||
        !update_int_field(wol, "port", 1, 65535, params.wol.port, err)) {
      return reply_error(req, http_deal::http::status::bad_request, err);
    }
  }

  if (body.contains("ssh")) {
    if (!body["ssh"].is_object()) {
      return reply_error(req, http_deal::http::status::bad_request, "field ssh must be object");
    }
    const auto& ssh = body["ssh"];
    std::string err;
    if (!update_string_field(ssh, "host", params.ssh.host, err) ||
        !update_int_field(ssh, "port", 1, 65535, params.ssh.port, err) ||
        !update_string_field(ssh, "user", params.ssh.user, err) ||
        !update_string_field(ssh, "password", params.ssh.password, err) ||
        !update_int_field(ssh, "timeout_ms", 100, 600000, params.ssh.timeout_ms, err)) {
      return reply_error(req, http_deal::http::status::bad_request, err);
    }
  }

  std::string save_err;
  if (!service::save_control_params(params, save_err)) {
    return reply_error(
        req,
        http_deal::http::status::internal_server_error,
        "save params failed: " + save_err);
  }

  return reply_ok(req, params_to_json(params));
}

} // namespace api
