#include "app/runtime.h"

#include "app/bootstrap/runtime_impl.h"

#include <cstdlib>
#include <memory>

namespace app {

Runtime::Runtime(const Config& config)
    : impl_(std::make_unique<bootstrap::RuntimeImpl>(config)) {}

Runtime::~Runtime() = default;

int Runtime::run() {
  if (!impl_) {
    return EXIT_FAILURE;
  }
  return impl_->run();
}

} // namespace app
