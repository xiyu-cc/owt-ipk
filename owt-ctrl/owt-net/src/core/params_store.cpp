#include "service/params_store.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>

namespace service {

namespace {

constexpr const char* kSystemParamsPath = "/etc/owt-net/params.ini";
constexpr const char* kLocalParamsPath = "params.ini";

std::mutex g_params_mutex;

std::string trim(std::string s) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

bool parse_int(const std::string& s, int& out) {
  try {
    size_t idx = 0;
    const int value = std::stoi(s, &idx);
    if (idx != s.size()) {
      return false;
    }
    out = value;
    return true;
  } catch (...) {
    return false;
  }
}

std::string choose_read_path() {
  std::ifstream sys(kSystemParamsPath);
  if (sys) {
    return kSystemParamsPath;
  }
  return kLocalParamsPath;
}

std::string choose_write_path() {
  if (::access("/etc/owt-net", W_OK) == 0) {
    return kSystemParamsPath;
  }
  return kLocalParamsPath;
}

void assign_if_nonempty(std::string& target, const std::string& value) {
  if (!value.empty()) {
    target = value;
  }
}

} // namespace

control_params load_control_params() {
  std::lock_guard<std::mutex> lk(g_params_mutex);

  control_params params;

  std::ifstream in(choose_read_path());
  if (!in) {
    return params;
  }

  std::string section;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;
    }

    if (line.front() == '[' && line.back() == ']') {
      section = to_lower(trim(line.substr(1, line.size() - 2)));
      continue;
    }

    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    const std::string key = to_lower(trim(line.substr(0, eq)));
    const std::string value = trim(line.substr(eq + 1));

    if (section == "wol") {
      if (key == "mac") {
        assign_if_nonempty(params.wol.mac, value);
      } else if (key == "broadcast") {
        assign_if_nonempty(params.wol.broadcast, value);
      } else if (key == "port") {
        int parsed = 0;
        if (parse_int(value, parsed) && parsed > 0 && parsed <= 65535) {
          params.wol.port = parsed;
        }
      }
      continue;
    }

    if (section == "ssh") {
      if (key == "host") {
        params.ssh.host = value;
      } else if (key == "port") {
        int parsed = 0;
        if (parse_int(value, parsed) && parsed > 0 && parsed <= 65535) {
          params.ssh.port = parsed;
        }
      } else if (key == "user") {
        params.ssh.user = value;
      } else if (key == "password") {
        params.ssh.password = value;
      } else if (key == "timeout_ms") {
        int parsed = 0;
        if (parse_int(value, parsed) && parsed > 0 &&
            parsed <= std::numeric_limits<int>::max()) {
          params.ssh.timeout_ms = parsed;
        }
      }
    }
  }

  return params;
}

bool save_control_params(const control_params& params, std::string& error) {
  std::lock_guard<std::mutex> lk(g_params_mutex);

  const std::string path = choose_write_path();
  const std::string tmp = path + ".tmp";

  std::ofstream out(tmp, std::ios::trunc);
  if (!out) {
    error = "open temp file failed: " + tmp;
    return false;
  }

  out << "[wol]\n";
  out << "mac = " << params.wol.mac << "\n";
  out << "broadcast = " << params.wol.broadcast << "\n";
  out << "port = " << params.wol.port << "\n\n";

  out << "[ssh]\n";
  out << "host = " << params.ssh.host << "\n";
  out << "port = " << params.ssh.port << "\n";
  out << "user = " << params.ssh.user << "\n";
  out << "password = " << params.ssh.password << "\n";
  out << "timeout_ms = " << params.ssh.timeout_ms << "\n";

  out.close();
  if (!out) {
    error = "write temp file failed: " + tmp;
    return false;
  }

  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::remove(tmp.c_str());
    error = "rename temp file failed: " + path;
    return false;
  }

  return true;
}

} // namespace service
