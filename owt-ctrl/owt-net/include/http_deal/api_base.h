#pragma once
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

namespace http_deal
{

namespace beast = boost::beast;
namespace http = beast::http;
using method = http::verb;

template <method value, typename Body>
class api_base
{
public:
    api_base() = default;
    virtual ~api_base() = default;

    virtual http::message_generator operator()(http::request<Body>& req) = 0;
};

} // namespace http_deal
