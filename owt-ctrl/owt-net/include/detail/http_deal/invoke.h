#pragma once
#include "detail/http_deal/factory.h"
#include "detail/http_deal/handler.h"
#include "detail/http_deal/response.h"
#include "detail/runtime/utils.h"
#include <boost/algorithm/string.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

namespace http_deal {

template <method value, class Body> class invoke {
public:
  http::message_generator operator()(http::request<Body> &req) {
    auto handler_ptr = factory<std::string, handler<value, Body>>::get(
        utils::url_path(req.target()));
    if (nullptr == handler_ptr)
      return response::message(req, http::status::not_found, "");

    return (*handler_ptr)(req);
  };
};

} // namespace http_deal
