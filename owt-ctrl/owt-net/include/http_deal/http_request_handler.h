#pragma once
#include "http_deal/api_invoke.h"
#include "http_deal/api_invoke_options.h"
#include "http_deal/response.h"
#include "log.h"
#include <memory>

namespace http_deal
{

class http_request_handler
{
protected:
    http_request_handler() = delete;
    http_request_handler(const http_request_handler&) = delete;
    http_request_handler(http_request_handler&&) =delete;
    void operator= (const http_request_handler&) = delete;
    void operator= (http_request_handler&&) = delete;
    ~http_request_handler() = delete;

public:
    template <class Body>
    static http::message_generator deal(
        http::request<Body>& req)
    {
        switch (req.method())
        {
        case method::options:
            return api_invoke<method::options, Body>()(req);
            
        case method::get:
            return api_invoke<method::get, Body>()(req);

        case method::post:
            return api_invoke<method::post, Body>()(req);

        case method::put:
            return api_invoke<method::put, Body>()(req);
        
        default:
            break;
        }

        log::info("http_request_handler: unknown method {}", static_cast<int>(req.method()));
        return response::message(req, http::status::not_found, "");
    }

};

} // namespace http_deal