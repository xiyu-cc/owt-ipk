#include "app/config.h"
#include "app/runtime.h"

#include <string>

int main(int argc, char* argv[]) {
  std::string config_path = "config.ini";
  if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
    config_path = argv[1];
  }

  const auto config = app::load_config(config_path);
  app::Runtime runtime(config);
  return runtime.run();
}

