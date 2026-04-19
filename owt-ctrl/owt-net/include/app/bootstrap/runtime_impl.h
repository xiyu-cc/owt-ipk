#pragma once

#include "app/config.h"

namespace app::bootstrap {

class RuntimeImpl {
public:
  explicit RuntimeImpl(const Config& config);
  ~RuntimeImpl();

  RuntimeImpl(const RuntimeImpl&) = delete;
  RuntimeImpl& operator=(const RuntimeImpl&) = delete;

  int run();

private:
  struct State;
  State* state_ = nullptr;
};

} // namespace app::bootstrap
