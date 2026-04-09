#pragma once

#include <memory>
#include <mutex>
#include <string>

namespace server {

class grpc_control_server {
public:
  grpc_control_server();
  ~grpc_control_server();

  bool start(const std::string& endpoint);
  void stop();
  bool is_running() const;

private:
  class impl;
  std::unique_ptr<impl> impl_;
  mutable std::mutex mutex_;
};

} // namespace server
