#include "api/wol_wake_api.h"

#include "api/api_common.h"
#include "service/wakeonlan_sender.h"

#include <boost/asio/ip/address.hpp>
#include <nlohmann/json.hpp>

#include <string>

namespace api {

namespace {

bool is_valid_mac(const std::string& mac) {
  return service::parse_mac_address(mac).has_value();
}

bool is_valid_ipv4(const std::string& addr) {
  boost::system::error_code ec;
  auto parsed = boost::asio::ip::make_address(addr, ec);
  return !ec && parsed.is_v4();
}

} // namespace

http_deal::http::message_generator wol_wake_api::operator()(request_t& req) {
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(body_as_string(req));
  } catch (const std::exception&) {
    return reply_error(req, http_deal::http::status::bad_request, "invalid json body");
  }

  const std::string mac = body.value("mac", "");
  const std::string broadcast = body.value("broadcast", "255.255.255.255");
  const int port = body.value("port", 9);

  if (!is_valid_mac(mac)) {
    return reply_error(req, http_deal::http::status::bad_request, "invalid mac");
  }
  if (!is_valid_ipv4(broadcast)) {
    return reply_error(req, http_deal::http::status::bad_request, "invalid broadcast ip");
  }
  if (port <= 0 || port > 65535) {
    return reply_error(req, http_deal::http::status::bad_request, "invalid wol port");
  }

  const auto res = service::send_magic_packet({mac, broadcast, port});
  if (!res.ok) {
    return reply_error(
        req,
        http_deal::http::status::internal_server_error,
        "send magic packet failed: " + res.error);
  }

  nlohmann::json data = {
      {"mac", mac},
      {"broadcast", broadcast},
      {"port", port},
      {"bytes_sent", res.bytes_sent},
  };
  return reply_ok(req, data);
}

} // namespace api
