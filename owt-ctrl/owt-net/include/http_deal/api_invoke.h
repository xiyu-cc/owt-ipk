#pragma once
#include "http_deal/api_base.h"
#include "http_deal/api_factory.h"
#include "http_deal/response.h"
#include "service/auth.h"
#include "service/observability.h"
#include "service/rate_limiter.h"
#include "utils.h"
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/algorithm/string.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <string>

namespace http_deal
{

template <method value, class Body>
class api_invoke
{
public:
    http::message_generator operator()(http::request<Body>& req)
    {
        service::record_http_request();
        if (!service::is_http_request_authorized(req)) {
            nlohmann::json body = {
                {"ok", false},
                {"code", 401},
                {"message", "unauthorized: missing trusted Google identity headers"},
            };
            return response::message(req, http::status::unauthorized, body);
        }
        if (service::is_http_rate_limit_enabled()) {
            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
            int64_t retry_after_ms = 0;
            const auto limiter_key = resolve_actor_key(req);
            if (!service::allow_http_request(limiter_key, now_ms, retry_after_ms)) {
                service::record_rate_limited(limiter_key, retry_after_ms);
                nlohmann::json body = {
                    {"ok", false},
                    {"code", 429},
                    {"message", "rate limit exceeded"},
                    {"retry_after_ms", retry_after_ms},
                };
                return response::message(req, http::status::too_many_requests, body);
            }
        }

        auto api_ptr = factory<std::string, api_base<value, Body>>::get(utils::url_path(req.target()));
        if (nullptr == api_ptr) return response::message(req, http::status::not_found, "");

        return (*api_ptr)(req);
    };

private:
    static std::string resolve_actor_key(const http::request<Body>& req) {
        const auto xff = req.find("X-Forwarded-For");
        if (xff != req.end() && !xff->value().empty()) {
            return std::string(xff->value());
        }
        const auto xri = req.find("X-Real-IP");
        if (xri != req.end() && !xri->value().empty()) {
            return std::string(xri->value());
        }
        return "unknown";
    }
};

} // namespace http_deal
