#pragma once
#include "detail/http_deal/invoke.h"

namespace http_deal {

template <class Body> class invoke<method::options, Body> {
public:
  http::message_generator operator()(http::request<Body> &req) {
    http::response<http::empty_body> res{http::status::no_content,
                                         req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods,
            "POST, GET, OPTIONS, PUT");
    res.set(http::field::access_control_allow_headers,
            "Content-Type, Authorization");
    res.set(http::field::access_control_max_age, "86400"); // 预检缓存时间（秒）
    res.keep_alive(req.keep_alive());
    return res;
  };
};

} // namespace http_deal
