#pragma once

#include "http_deal/response.h"

#include <nlohmann/json.hpp>
#include <string>

namespace api {

using request_t = http_deal::http::request<http_deal::http::vector_body<uint8_t>>;

inline std::string body_as_string(const request_t& req) {
  return std::string(req.body().begin(), req.body().end());
}

inline http_deal::http::message_generator reply_error(
    request_t& req,
    http_deal::http::status status,
    const std::string& message,
    int code = -1) {
  nlohmann::json j = {
      {"ok", false},
      {"code", code},
      {"message", message},
  };
  return http_deal::response::message(req, status, j);
}

inline http_deal::http::message_generator reply_ok(
    request_t& req,
    const nlohmann::json& data = nlohmann::json::object()) {
  nlohmann::json j = {
      {"ok", true},
      {"code", 0},
      {"message", "success"},
      {"data", data},
  };
  return http_deal::response::message(req, http_deal::http::status::ok, j);
}

} // namespace api
