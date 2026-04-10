#pragma once

#include <boost/beast/http.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace service {

namespace detail {

inline std::string trim_ascii(std::string text) {
  const auto is_not_space = [](unsigned char c) { return !std::isspace(c); };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), is_not_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), is_not_space).base(), text.end());
  return text;
}

inline std::string to_lower_ascii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

inline bool is_forwarded_proto_https(const std::string& raw_value) {
  const auto token_end = raw_value.find(',');
  const auto token = token_end == std::string::npos ? raw_value : raw_value.substr(0, token_end);
  return to_lower_ascii(trim_ascii(token)) == "https";
}

template <class Body, class Fields>
bool has_nonempty_header(
    const boost::beast::http::request<Body, Fields>& req, boost::beast::string_view key) {
  const auto it = req.find(key);
  return it != req.end() && !trim_ascii(std::string(it->value())).empty();
}

} // namespace detail

template <class Body, class Fields>
bool is_http_request_authorized(const boost::beast::http::request<Body, Fields>& req) {
  const auto proto = req.find("X-Forwarded-Proto");
  if (proto == req.end() || !detail::is_forwarded_proto_https(std::string(proto->value()))) {
    return false;
  }

  const bool has_google_identity =
      detail::has_nonempty_header(req, "X-Forwarded-Email") ||
      detail::has_nonempty_header(req, "X-Forwarded-User") ||
      detail::has_nonempty_header(req, "X-Auth-Request-Email") ||
      detail::has_nonempty_header(req, "X-Auth-Request-User");
  return has_google_identity;
}

} // namespace service
