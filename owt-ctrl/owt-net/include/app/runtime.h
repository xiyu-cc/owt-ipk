#pragma once

#include "app/config.h"

namespace app {

class Runtime {
public:
  explicit Runtime(const Config& config);
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  int run();

private:
  void* impl_ = nullptr;
};

} // namespace app
