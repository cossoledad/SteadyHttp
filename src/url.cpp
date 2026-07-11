#include "url.hpp"

#include <boost/url.hpp>
#include <charconv>

namespace steady_http::detail {
namespace {
Error url_error(ErrorCode code, std::string message) {
    return Error{.code = code,
                 .stage = TransferStage::parse_url,
                 .message = std::move(message),
                 .system_error = {},
                 .http_status = std::nullopt,
                 .attempt = 0,
                 .redirect_count = 0,
                 .retryable = false,
                 .request_may_have_been_processed = false};
}
}  // namespace

Result<ParsedUrl> parse_url(const std::string& text) {
    const auto parsed = boost::urls::parse_uri(text);
    if (!parsed) {
        return url_error(ErrorCode::invalid_url, "URL parsing failed");
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
        return url_error(ErrorCode::unsupported_scheme, "URL scheme must be http or https");
    }
    if (url.host().empty()) {
        return url_error(ErrorCode::invalid_url, "URL host is empty");
    }
    if (url.has_port() && url.port().empty()) {
        return url_error(ErrorCode::invalid_url, "URL port is invalid");
    }
    if (url.has_port()) {
        unsigned port_number = 0;
        const auto port_text = url.port();
        const auto [end, error] =
            std::from_chars(port_text.data(), port_text.data() + port_text.size(), port_number);
        if (error != std::errc{} || end != port_text.data() + port_text.size() ||
            port_number == 0 || port_number > 65535) {
            return url_error(ErrorCode::invalid_url, "URL port must be an integer from 1 to 65535");
        }
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
