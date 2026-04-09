#include "service/wakeonlan_sender.h"

// Magic packet format aligned to upstream wakeonlan (jpoliv/wakeonlan v0.42):
// 6 x 0xFF followed by 16 repetitions of MAC address bytes.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <string>

namespace service {

namespace {

int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

} // namespace

std::optional<std::array<uint8_t, 6>> parse_mac_address(const std::string& mac_text) {
  std::string compact;
  compact.reserve(mac_text.size());

  for (char c : mac_text) {
    if (c == ':' || c == '-') {
      continue;
    }
    compact.push_back(c);
  }

  if (compact.size() != 12) {
    return std::nullopt;
  }

  std::array<uint8_t, 6> mac{};
  for (size_t i = 0; i < 6; ++i) {
    const int hi = hex_value(compact[i * 2]);
    const int lo = hex_value(compact[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return std::nullopt;
    }
    mac[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return mac;
}

wakeonlan_result send_magic_packet(const wakeonlan_request& req) {
  wakeonlan_result result;

  if (req.port <= 0 || req.port > 65535) {
    result.error = "invalid wol port";
    return result;
  }

  const auto mac_opt = parse_mac_address(req.mac);
  if (!mac_opt.has_value()) {
    result.error = "invalid mac address";
    return result;
  }

  std::array<uint8_t, 102> packet{};
  std::fill_n(packet.begin(), 6, 0xFF);
  for (size_t i = 0; i < 16; ++i) {
    std::copy(mac_opt->begin(), mac_opt->end(), packet.begin() + 6 + i * 6);
  }

  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    result.error = std::string("socket failed: ") + std::strerror(errno);
    return result;
  }

  int enable_broadcast = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enable_broadcast, sizeof(enable_broadcast)) != 0) {
    result.error = std::string("setsockopt(SO_BROADCAST) failed: ") + std::strerror(errno);
    close(fd);
    return result;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(req.port));
  if (inet_pton(AF_INET, req.broadcast_ip.c_str(), &addr.sin_addr) != 1) {
    result.error = "invalid broadcast ip";
    close(fd);
    return result;
  }

  const ssize_t n = sendto(
      fd,
      packet.data(),
      packet.size(),
      0,
      reinterpret_cast<const sockaddr*>(&addr),
      sizeof(addr));
  if (n < 0) {
    result.error = std::string("sendto failed: ") + std::strerror(errno);
    close(fd);
    return result;
  }

  result.ok = true;
  result.bytes_sent = static_cast<int>(n);
  close(fd);
  return result;
}

} // namespace service
