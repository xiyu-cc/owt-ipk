#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace service {

struct wakeonlan_request {
  std::string mac;
  std::string broadcast_ip;
  int port = 9;
};

struct wakeonlan_result {
  bool ok = false;
  int bytes_sent = 0;
  std::string error;
};

std::optional<std::array<uint8_t, 6>> parse_mac_address(const std::string& mac_text);
wakeonlan_result send_magic_packet(const wakeonlan_request& req);

} // namespace service
