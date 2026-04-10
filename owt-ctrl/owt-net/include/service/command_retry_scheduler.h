#pragma once

namespace service {

bool start_command_retry_scheduler();
void stop_command_retry_scheduler();
bool is_command_retry_scheduler_running();

} // namespace service
