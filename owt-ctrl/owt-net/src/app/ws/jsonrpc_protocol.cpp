#include "app/ws/jsonrpc_protocol.h"
#include "json_field_validation.h"

namespace app::ws {

namespace {

bool is_valid_id(const nlohmann::json& id) {
  return id.is_null() || id.is_string() || id.is_number_integer() || id.is_number_unsigned();
}

} // namespace

bool parse_jsonrpc_request(
    std::string_view text,
    JsonRpcRequest& out,
    std::string& error,
    int& error_code) {
  error.clear();
  error_code = -32600;

  auto root = nlohmann::json::parse(text, nullptr, false);
  if (!root.is_object()) {
    error = "invalid json";
    error_code = -32700;
    return false;
  }

  std::string unknown;
  if (!detail::reject_unknown_fields(root, {"jsonrpc", "id", "method", "params"}, unknown)) {
    error = "unknown field: " + unknown;
    error_code = -32600;
    return false;
  }

  if (!root.contains("jsonrpc") || !root["jsonrpc"].is_string() ||
      root["jsonrpc"].get<std::string>() != "2.0") {
    error = "jsonrpc must be 2.0";
    error_code = -32600;
    return false;
  }
  if (!root.contains("method") || !root["method"].is_string() ||
      root["method"].get<std::string>().empty()) {
    error = "method is required";
    error_code = -32600;
    return false;
  }
  if (root.contains("params") && !root["params"].is_object()) {
    error = "params must be object";
    error_code = -32602;
    return false;
  }

  out.method = root["method"].get<std::string>();
  out.params = root.value("params", nlohmann::json::object());

  if (root.contains("id")) {
    if (!is_valid_id(root["id"])) {
      error = "id must be string, integer or null";
      error_code = -32600;
      return false;
    }
    out.id = root["id"];
    out.notification = false;
  } else {
    out.id = nullptr;
    out.notification = true;
  }

  return true;
}

std::string jsonrpc_result(
    const nlohmann::json& id,
    const nlohmann::json& resource,
    const nlohmann::json& meta) {
  return nlohmann::json{
      {"jsonrpc", "2.0"},
      {"id", id},
      {"result", {{"resource", resource}, {"meta", meta.is_object() ? meta : nlohmann::json::object()}}},
  }
      .dump();
}

std::string jsonrpc_error(
    const nlohmann::json& id,
    int code,
    std::string message,
    const nlohmann::json& data) {
  return nlohmann::json{
      {"jsonrpc", "2.0"},
      {"id", id},
      {"error",
       {
           {"code", code},
           {"message", std::move(message)},
           {"data", data.is_object() ? data : nlohmann::json::object()},
       }},
  }
      .dump();
}

std::string jsonrpc_notify(std::string method, const nlohmann::json& params) {
  return nlohmann::json{
      {"jsonrpc", "2.0"},
      {"method", std::move(method)},
      {"params", params},
  }
      .dump();
}

} // namespace app::ws
