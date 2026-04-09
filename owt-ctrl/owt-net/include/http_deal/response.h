#pragma once
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <nlohmann/json.hpp>

namespace http_deal
{
namespace beast = boost::beast;
namespace http = beast::http;
namespace nlo = nlohmann;

namespace response
{
    template <typename Body>
    http::message_generator message(http::request<Body>& req, http::status stat, const char* load)
    {
        http::response<http::string_body> res{stat, req.version()};
        res.set(http::field::access_control_allow_origin,  "*");
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(load);
        res.prepare_payload();
        return res;
    }

    template <typename Body>
    http::message_generator message(http::request<Body>& req, http::status stat, const std::string& load)
    {
        http::response<http::string_body> res{stat, req.version()};
        res.set(http::field::access_control_allow_origin,  "*");
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = load;
        res.prepare_payload();
        return res;
    }

    template <typename Body>
    http::message_generator message(http::request<Body>& req, http::status stat, const nlo::json& load)
    {
        http::response<http::string_body> res{stat, req.version()};
        res.set(http::field::access_control_allow_origin,  "*");
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "application/json");
        res.keep_alive(req.keep_alive());
        res.body() = load.dump();
        res.prepare_payload();
        return res;
    }

    template <typename Body>
    http::message_generator message_file(http::request<Body>& req, const std::string& type, const std::string& file)
    {
        http::response<http::file_body> res{http::status::ok, req.version()};
        res.set(http::field::access_control_allow_origin,  "*");
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, type);
        res.keep_alive(req.keep_alive());
        
        beast::error_code ec;
        res.body().open(std::string("storage/file/" + file).c_str(), beast::file_mode::read, ec);

        if (ec) 
        {
            return message(req, http::status::not_found, "");
        }

        res.prepare_payload();
        return res;
    }
    
} // namespace message

    
} // namespace http_deal
