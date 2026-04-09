#include "utils.h"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <cctype>
#include <iomanip>
#include <sstream>

namespace utils {

std::vector<std::string> split(const std::string& s, char delimiter) {
  std::vector<std::string> tokens;
  std::istringstream iss(s);
  std::string token;
  while (std::getline(iss, token, delimiter)) {
    if (!token.empty()) {
      tokens.push_back(token);
    }
  }
  return tokens;
}

std::string uuid() {
  static thread_local boost::uuids::random_generator generator;
  return boost::uuids::to_string(generator());
}

std::string url_path(const std::string& url) {
  const auto pos = url.find('?');
  if (pos == std::string::npos) {
    return url;
  }
  return url.substr(0, pos);
}

namespace {

int hexCharToInt(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return -1;
}

std::string decodeComponent(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    const char c = input[i];
    if (c == '+') {
      out.push_back(' ');
      continue;
    }
    if (c == '%' && i + 2 < input.size()) {
      const int hi = hexCharToInt(input[i + 1]);
      const int lo = hexCharToInt(input[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(c);
  }
  return out;
}

} // namespace

std::map<std::string, std::string> url_argument(const std::string& url) {
  std::map<std::string, std::string> map_arg;
  const auto pos = url.find('?');
  if (pos == std::string::npos) {
    return map_arg;
  }

  const auto pairs = split(url.substr(pos + 1), '&');
  for (const auto& pair : pairs) {
    const auto equal_pos = pair.find('=');
    if (equal_pos == std::string::npos) {
      map_arg[decodeComponent(pair)] = "";
      continue;
    }
    const std::string key = decodeComponent(pair.substr(0, equal_pos));
    const std::string value = decodeComponent(pair.substr(equal_pos + 1));
    map_arg[key] = value;
  }
  return map_arg;
}

int64_t time_stamp(const boost::posix_time::ptime& time_data) {
  static const boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
  return (time_data - epoch).total_milliseconds();
}

boost::posix_time::ptime boost_ptime(int64_t millis) {
  static const boost::posix_time::ptime epoch(boost::gregorian::date(1970, 1, 1));
  return epoch + boost::posix_time::milliseconds(millis);
}

std::string uri_encode(const std::string& str) {
  static const std::string illegal = "%<>{}|\\\"^`!*'()$,[]";
  static const std::string reserved = "()[]/|\\',;";

  std::ostringstream oss;
  oss << std::uppercase << std::hex;
  for (const unsigned char c : str) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      oss << static_cast<char>(c);
      continue;
    }
    if (c <= 0x20 || c >= 0x7F || illegal.find(static_cast<char>(c)) != std::string::npos ||
        reserved.find(static_cast<char>(c)) != std::string::npos) {
      oss << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
      continue;
    }
    oss << static_cast<char>(c);
  }
  return oss.str();
}

boost::optional<std::string> uri_decode(const std::string& str, bool plus_as_space) {
  std::string out;
  out.reserve(str.size());

  bool in_query = false;
  for (size_t i = 0; i < str.size(); ++i) {
    char c = str[i];
    if (c == '?') {
      in_query = true;
      out.push_back(c);
      continue;
    }
    if (in_query && plus_as_space && c == '+') {
      out.push_back(' ');
      continue;
    }
    if (c == '%') {
      if (i + 2 >= str.size()) {
        return boost::none;
      }
      const int hi = hexCharToInt(str[i + 1]);
      const int lo = hexCharToInt(str[i + 2]);
      if (hi < 0 || lo < 0) {
        return boost::none;
      }
      out.push_back(static_cast<char>((hi << 4) | lo));
      i += 2;
      continue;
    }
    out.push_back(c);
  }
  return out;
}

} // namespace utils
