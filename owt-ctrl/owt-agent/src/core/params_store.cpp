#include "service/params_store.h"

#include <mutex>

namespace service {

namespace {

std::mutex g_params_mutex;
bool g_params_initialized = false;
control_params g_runtime_params;

} // namespace

control_params load_control_params() {
  std::lock_guard<std::mutex> lk(g_params_mutex);
  if (!g_params_initialized) {
    g_runtime_params = control_params{};
    g_params_initialized = true;
  }
  return g_runtime_params;
}

bool save_control_params(const control_params& params, std::string& error) {
  std::lock_guard<std::mutex> lk(g_params_mutex);
  g_runtime_params = params;
  g_params_initialized = true;
  error.clear();
  return true;
}

} // namespace service
