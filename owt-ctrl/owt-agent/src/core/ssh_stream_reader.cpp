#include "service/ssh_stream_reader.h"

#include <algorithm>

namespace service::ssh_stream_reader {

void append_stream(
    const stream_reader& reader,
    long long eagain_code,
    std::string& target) {
  if (!reader) {
    return;
  }

  char buffer[4096];
  while (true) {
    const auto n = reader(buffer, sizeof(buffer));
    if (n > 0) {
      const auto chunk_size = static_cast<std::size_t>(std::min<long long>(n, sizeof(buffer)));
      target.append(buffer, chunk_size);
      continue;
    }
    if (n == eagain_code) {
      continue;
    }
    break;
  }
}

void collect_streams(
    const stream_reader& stdout_reader,
    const stream_reader& stderr_reader,
    long long eagain_code,
    std::string& output,
    std::string& error) {
  append_stream(stdout_reader, eagain_code, output);
  append_stream(stderr_reader, eagain_code, error);
}

} // namespace service::ssh_stream_reader
