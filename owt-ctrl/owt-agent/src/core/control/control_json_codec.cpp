#include "control/control_json_codec.h"

#include "control/json_codec/codec_detail.h"

#include <exception>

namespace control {

std::string encode_envelope_json(const envelope& value) {
  return json_codec::detail::envelope_to_json(value).dump();
}

bool decode_envelope_json(const std::string& text, envelope& out, std::string& error) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(text);
  } catch (const std::exception& e) {
    error = std::string("parse json failed: ") + e.what();
    return false;
  }
  return json_codec::detail::envelope_from_json(root, out, error);
}

} // namespace control
