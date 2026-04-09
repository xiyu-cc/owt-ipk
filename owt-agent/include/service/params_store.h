#pragma once

#include <string>

namespace service {

struct wol_params {
  std::string mac = "AA:BB:CC:DD:EE:FF";
  std::string broadcast = "192.168.1.255";
  int port = 9;
};

struct ssh_params {
  std::string host = "192.168.1.10";
  int port = 22;
  std::string user = "root";
  std::string password = "password";
  int timeout_ms = 5000;
};

struct control_params {
  wol_params wol;
  ssh_params ssh;
};

control_params load_control_params();
bool save_control_params(const control_params& params, std::string& error);

} // namespace service
