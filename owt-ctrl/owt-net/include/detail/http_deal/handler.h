#pragma once
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

namespace http_deal {

namespace beast = boost::beast;
namespace http = beast::http;
using method = http::verb;

template <method value, typename Body> class handler {
public:
  handler() = default;
  virtual ~handler() = default;

  virtual http::message_generator operator()(http::request<Body> &req) = 0;
};

} // namespace http_deal
