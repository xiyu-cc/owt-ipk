#pragma once

#include "control/control_protocol.h"

#if OWT_CTRL_ENABLE_GRPC_TRANSPORT
#include "control_channel.pb.h"
#endif

#include <string>

namespace control {

#if OWT_CTRL_ENABLE_GRPC_TRANSPORT
bool encode_envelope_proto(
    const envelope& input,
    owt::control::v1::Envelope& out,
    std::string& error);
bool decode_envelope_proto(
    const owt::control::v1::Envelope& input,
    envelope& out,
    std::string& error);
#endif

} // namespace control
