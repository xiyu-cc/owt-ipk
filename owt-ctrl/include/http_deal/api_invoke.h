#pragma once
#include "http_deal/api_base.h"
#include "http_deal/api_factory.h"
#include "http_deal/response.h"
#include "utils.h"
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/algorithm/string.hpp>

namespace http_deal
{

template <method value, class Body>
class api_invoke
{
public:
    http::message_generator operator()(http::request<Body>& req)
    {
        auto api_ptr = factory<std::string, api_base<value, Body>>::get(utils::url_path(req.target()));
        if (nullptr == api_ptr) return response::message(req, http::status::not_found, "");

        return (*api_ptr)(req);
    };
};

} // namespace http_deal
