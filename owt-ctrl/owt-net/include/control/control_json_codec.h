#pragma once

#include "control/control_protocol.h"

#include <string>

namespace control {

std::string encode_envelope_json(const envelope& value);
bool decode_envelope_json(const std::string& text, envelope& out, std::string& error);

} // namespace control

