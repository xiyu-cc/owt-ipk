#pragma once

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/optional.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace utils {

std::vector<std::string> split(const std::string &s, char delimiter);

std::string uuid();

std::string url_path(const std::string &url);

std::map<std::string, std::string> url_argument(const std::string &url);

int64_t time_stamp(const boost::posix_time::ptime &time_data);

boost::posix_time::ptime boost_ptime(int64_t millis);

std::string uri_encode(const std::string &str);

boost::optional<std::string> uri_decode(const std::string &str,
                                        bool plus_as_space = false);

} // namespace utils

#include "detail/runtime/utils_impl.h"
