#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace app::ws {

struct JsonRpcRequest {
  nlohmann::json id = nullptr;
  bool notification = true;
  std::string method;
  nlohmann::json params = nlohmann::json::object();
};

bool parse_jsonrpc_request(
    std::string_view text,
    JsonRpcRequest& out,
    std::string& error,
    int& error_code);

std::string jsonrpc_result(
    const nlohmann::json& id,
    const nlohmann::json& resource,
    const nlohmann::json& meta = nlohmann::json::object());

std::string jsonrpc_error(
    const nlohmann::json& id,
    int code,
    std::string message,
    const nlohmann::json& data = nlohmann::json::object());

std::string jsonrpc_notify(std::string method, const nlohmann::json& params);

} // namespace app::ws
