#include "url.hpp"

#include <boost/url.hpp>

namespace steady_http::detail {

Result<ParsedUrl> parse_url(const std::string& text) {
    const auto parsed = boost::urls::parse_uri(text);
    if (!parsed) {
        return Error{.code = ErrorCode::invalid_url,
                     .stage = TransferStage::parse_url,
                     .message = "URL parsing failed"};
    }
    const auto url = parsed.value();
    Scheme scheme;
    std::string default_port;
    if (url.scheme() == "http") {
        scheme = Scheme::http;
        default_port = "80";
    } else if (url.scheme() == "https") {
        scheme = Scheme::https;
        default_port = "443";
    } else {
        return Error{.code = ErrorCode::unsupported_scheme,
                     .stage = TransferStage::parse_url,
                     .message = "URL scheme must be http or https"};
    }
    if (url.host().empty()) {
        return Error{.code = ErrorCode::invalid_url,
                     .stage = TransferStage::parse_url,
                     .message = "URL host is empty"};
    }
    if (url.has_port() && url.port().empty()) {
        return Error{.code = ErrorCode::invalid_url,
                     .stage = TransferStage::parse_url,
                     .message = "URL port is invalid"};
    }
    std::string host{url.host()};
    const std::string port = url.has_port() ? std::string{url.port()} : default_port;
    std::string target{url.encoded_target()};
    if (target.empty()) {
        target = "/";
    }
    const bool ipv6 = url.host_type() == boost::urls::host_type::ipv6;
    if (ipv6 && host.size() >= 2) {
        host = host.substr(1, host.size() - 2);
    }
    std::string host_header = ipv6 ? "[" + host + "]" : host;
    if (port != default_port) {
        host_header += ":" + port;
    }
    return ParsedUrl{.scheme = scheme,
                     .host = host,
                     .port = port,
                     .target = std::move(target),
                     .host_header = std::move(host_header)};
}

}  // namespace steady_http::detail
