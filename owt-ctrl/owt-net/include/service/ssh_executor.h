#pragma once

#include <string>

namespace service {

struct ssh_request {
  std::string host;
  int port = 22;
  std::string user;
  std::string password;
  std::string command;
  int timeout_ms = 5000;
};

struct ssh_result {
  bool ok = false;
  int exit_status = -1;
  std::string output;
  std::string error;
};

ssh_result run_ssh_command(const ssh_request& req);

} // namespace service
