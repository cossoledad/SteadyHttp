#pragma once

#include <steady_http/result.hpp>

#include <string>

namespace steady_http::detail {

enum class Scheme { http, https };

struct ParsedUrl {
    Scheme scheme;
    std::string host;
    std::string port;
    std::string target;
    std::string host_header;
};

[[nodiscard]] Result<ParsedUrl> parse_url(const std::string& text);

}  // namespace steady_http::detail
