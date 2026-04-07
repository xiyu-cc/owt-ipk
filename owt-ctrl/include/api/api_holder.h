#pragma once
#include "api/host_poweroff_api.h"
#include "api/host_probe_api.h"
#include "api/host_reboot_api.h"
#include "api/monitoring_get_api.h"
#include "api/monitoring_set_api.h"
#include "api/params_get_api.h"
#include "api/params_set_api.h"
#include "api/wol_wake_api.h"
#include "http_deal/api_base.h"
#include "http_deal/api_factory.h"

#include <string>

namespace api
{
class api_holder
{
    using post_factory = http_deal::factory<std::string, http_deal::api_base<http_deal::method::post, http_deal::http::vector_body<uint8_t>>>;
    using put_factory = http_deal::factory<std::string, http_deal::api_base<http_deal::method::put, http_deal::http::vector_body<uint8_t>>>;
    using get_factory = http_deal::factory<std::string, http_deal::api_base<http_deal::method::get, http_deal::http::vector_body<uint8_t>>>;

public:
    api_holder()
    {
        post_factory::install(http_deal::inplace_hold<wol_wake_api>{}, "/api/v1/wol/wake");
        post_factory::install(http_deal::inplace_hold<host_reboot_api>{}, "/api/v1/host/reboot");
        post_factory::install(http_deal::inplace_hold<host_poweroff_api>{}, "/api/v1/host/poweroff");
        get_factory::install(http_deal::inplace_hold<host_probe_api>{}, "/api/v1/host/probe");
        get_factory::install(http_deal::inplace_hold<monitoring_get_api>{}, "/api/v1/monitoring/get");
        post_factory::install(http_deal::inplace_hold<monitoring_set_api>{}, "/api/v1/monitoring/set");
        get_factory::install(http_deal::inplace_hold<params_get_api>{}, "/api/v1/params/get");
        post_factory::install(http_deal::inplace_hold<params_set_api>{}, "/api/v1/params/set");
    }
    
    ~api_holder()
    {
        post_factory::reset();
        put_factory::reset();
        get_factory::reset();
    }
};

} // namespace api
