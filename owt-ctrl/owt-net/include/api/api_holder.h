#pragma once
#include "api/control_agent_get_api.h"
#include "api/control_agents_get_api.h"
#include "api/control_audit_get_api.h"
#include "api/control_command_get_api.h"
#include "api/control_command_push_api.h"
#include "api/control_commands_get_api.h"
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
        get_factory::install(http_deal::inplace_hold<control_agent_get_api>{}, "/api/v1/control/agent/get");
        get_factory::install(http_deal::inplace_hold<control_agents_get_api>{}, "/api/v1/control/agents/get");
        get_factory::install(http_deal::inplace_hold<control_command_get_api>{}, "/api/v1/control/command/get");
        get_factory::install(http_deal::inplace_hold<control_commands_get_api>{}, "/api/v1/control/commands/get");
        get_factory::install(http_deal::inplace_hold<control_audit_get_api>{}, "/api/v1/control/audit/get");
        post_factory::install(http_deal::inplace_hold<control_command_push_api>{}, "/api/v1/control/command/push");
    }
    
    ~api_holder()
    {
        post_factory::reset();
        put_factory::reset();
        get_factory::reset();
    }
};

} // namespace api
