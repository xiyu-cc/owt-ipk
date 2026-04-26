#pragma once

#include "app/config.h"

#include <memory>

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
  std::unique_ptr<State> state_;
};

} // namespace app::bootstrap
