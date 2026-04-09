#pragma once

#include "http_deal/api_base.h"

namespace api {

class params_set_api final
    : public http_deal::api_base<http_deal::method::post, http_deal::http::vector_body<uint8_t>> {
public:
  http_deal::http::message_generator operator()(http_deal::http::request<http_deal::http::vector_body<uint8_t>>& req) override;
};

} // namespace api
