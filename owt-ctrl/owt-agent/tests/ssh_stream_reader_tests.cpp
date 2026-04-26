#include "service/ssh_stream_reader.h"

#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

using stream_step = std::variant<long long, std::string>;

service::ssh_stream_reader::stream_reader make_scripted_reader(std::deque<stream_step> steps) {
  return [steps = std::move(steps)](char* buffer, std::size_t buffer_size) mutable -> long long {
    if (steps.empty()) {
      return 0;
    }
    auto step = std::move(steps.front());
    steps.pop_front();
    if (const auto* code = std::get_if<long long>(&step)) {
      return *code;
    }
    const auto& text = std::get<std::string>(step);
    const auto copy_size = std::min(buffer_size, text.size());
    std::memcpy(buffer, text.data(), copy_size);
    return static_cast<long long>(copy_size);
  };
}

void test_stdout_stderr_split() {
  constexpr long long kEagain = -37;
  auto stdout_reader = make_scripted_reader(
      {std::string("out-a"), kEagain, std::string("out-b"), 0LL});
  auto stderr_reader = make_scripted_reader(
      {std::string("err-a"), kEagain, std::string("err-b"), 0LL});

  std::string output;
  std::string error;
  service::ssh_stream_reader::collect_streams(
      stdout_reader,
      stderr_reader,
      kEagain,
      output,
      error);

  require(output == "out-aout-b", "stdout aggregation mismatch");
  require(error == "err-aerr-b", "stderr aggregation mismatch");
}

void test_eof_only_stream() {
  std::string output = "seed";
  service::ssh_stream_reader::append_stream(
      make_scripted_reader({0LL}),
      -1,
      output);
  require(output == "seed", "eof stream must keep output unchanged");
}

} // namespace

int main() {
  try {
    test_stdout_stderr_split();
    test_eof_only_stream();
    std::cout << "owt-agent ssh stream reader tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "owt-agent ssh stream reader tests failed: " << ex.what() << '\n';
    return 1;
  }
}
