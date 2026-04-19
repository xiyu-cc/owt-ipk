#include "app/runtime.h"

#include "app/bootstrap/runtime_impl.h"

#include <cstdlib>

namespace app {

Runtime::Runtime(const Config& config) : impl_(new bootstrap::RuntimeImpl(config)) {}

Runtime::~Runtime() {
  delete static_cast<bootstrap::RuntimeImpl*>(impl_);
  impl_ = nullptr;
}

int Runtime::run() {
  if (impl_ == nullptr) {
    return EXIT_FAILURE;
  }
  return static_cast<bootstrap::RuntimeImpl*>(impl_)->run();
}

} // namespace app
