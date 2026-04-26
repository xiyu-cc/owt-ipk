#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace service::ssh_stream_reader {

using stream_reader = std::function<long long(char*, std::size_t)>;

void append_stream(
    const stream_reader& reader,
    long long eagain_code,
    std::string& target);

void collect_streams(
    const stream_reader& stdout_reader,
    const stream_reader& stderr_reader,
    long long eagain_code,
    std::string& output,
    std::string& error);

} // namespace service::ssh_stream_reader
