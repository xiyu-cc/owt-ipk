#pragma once
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <nlohmann/json.hpp>

#include <string_view>

namespace http_deal {
namespace beast = boost::beast;
namespace http = beast::http;
namespace nlo = nlohmann;

namespace response {
namespace detail {

inline bool is_safe_file_name(std::string_view file) {
  if (file.empty()) {
    return false;
  }

  if (file.find("..") != std::string_view::npos ||
      file.find('/') != std::string_view::npos ||
      file.find('\\') != std::string_view::npos ||
      file.find(':') != std::string_view::npos) {
    return false;
  }

  return true;
}

} // namespace detail

template <typename Body>
http::message_generator message(http::request<Body> &req, http::status stat,
                                const char *load) {
  http::response<http::string_body> res{stat, req.version()};
  res.set(http::field::access_control_allow_origin, "*");
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, "text/html");
  res.keep_alive(req.keep_alive());
  res.body() = std::string(load);
  res.prepare_payload();
  return res;
}

template <typename Body>
http::message_generator message(http::request<Body> &req, http::status stat,
                                const std::string &load) {
  http::response<http::string_body> res{stat, req.version()};
  res.set(http::field::access_control_allow_origin, "*");
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, "text/html");
  res.keep_alive(req.keep_alive());
  res.body() = load;
  res.prepare_payload();
  return res;
}

template <typename Body>
http::message_generator message(http::request<Body> &req, http::status stat,
                                const nlo::json &load) {
  http::response<http::string_body> res{stat, req.version()};
  res.set(http::field::access_control_allow_origin, "*");
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, "application/json");
  res.keep_alive(req.keep_alive());
  res.body() = load.dump();
  res.prepare_payload();
  return res;
}

template <typename Body>
http::message_generator message_file(http::request<Body> &req,
                                     const std::string &type,
                                     const std::string &file) {
  if (!detail::is_safe_file_name(file)) {
    return message(req, http::status::bad_request, "");
  }

  http::response<http::file_body> res{http::status::ok, req.version()};
  res.set(http::field::access_control_allow_origin, "*");
  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, type);
  res.keep_alive(req.keep_alive());

  beast::error_code ec;
  res.body().open(std::string("storage/file/" + file).c_str(),
                  beast::file_mode::read, ec);

  if (ec) {
    return message(req, http::status::not_found, "");
  }

  res.prepare_payload();
  return res;
}

} // namespace response

} // namespace http_deal
