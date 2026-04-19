#pragma once
#include "detail/http_deal/invoke.h"
#include "detail/http_deal/invoke_options.h"
#include "detail/http_deal/response.h"
#include "detail/runtime/log.h"
#include <memory>

namespace http_deal {

class router {
protected:
  router() = delete;
  router(const router &) = delete;
  router(router &&) = delete;
  void operator=(const router &) = delete;
  void operator=(router &&) = delete;
  ~router() = delete;

public:
  template <class Body>
  static http::message_generator dispatch(http::request<Body> &req) {
    switch (req.method()) {
    case method::options:
      return invoke<method::options, Body>()(req);

    case method::get:
      return invoke<method::get, Body>()(req);

    case method::post:
      return invoke<method::post, Body>()(req);

    case method::put:
      return invoke<method::put, Body>()(req);

    default:
      break;
    }

    log::info("http router: unknown method {}",
              static_cast<int>(req.method()));
    return response::message(req, http::status::not_found, "");
  }
};

} // namespace http_deal
