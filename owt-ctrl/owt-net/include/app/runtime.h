#pragma once

#include "app/config.h"

#include <memory>

namespace app {

namespace bootstrap {
class RuntimeImpl;
}

class Runtime {
public:
  explicit Runtime(const Config& config);
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  int run();

private:
  std::unique_ptr<bootstrap::RuntimeImpl> impl_;
};

} // namespace app
