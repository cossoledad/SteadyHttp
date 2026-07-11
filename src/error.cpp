#include <cctype>
#include <steady_http/error.hpp>
#include <steady_http/response.hpp>

namespace steady_http {

std::string_view to_string(ErrorCode code) noexcept {
    using enum ErrorCode;
    switch (code) {
        case invalid_url:
            return "invalid_url";
        case unsupported_scheme:
            return "unsupported_scheme";
        case resolve_failed:
            return "resolve_failed";
        case connect_failed:
            return "connect_failed";
        case tls_configuration_failed:
            return "tls_configuration_failed";
        case tls_handshake_failed:
            return "tls_handshake_failed";
        case certificate_verification_failed:
            return "certificate_verification_failed";
        case write_failed:
            return "write_failed";
        case read_failed:
            return "read_failed";
        case timeout:
            return "timeout";
        case cancelled:
            return "cancelled";
        case response_too_large:
            return "response_too_large";
        case invalid_response:
            return "invalid_response";
        case too_many_redirects:
            return "too_many_redirects";
        case redirect_missing_location:
            return "redirect_missing_location";
        case http_status_error:
            return "http_status_error";
        case retry_exhausted:
            return "retry_exhausted";
        case client_stopped:
            return "client_stopped";
        case invalid_argument:
            return "invalid_argument";
        case internal_error:
            return "internal_error";
    }
    return "unknown";
}

std::string_view to_string(TransferStage stage) noexcept {
    using enum TransferStage;
    switch (stage) {
        case none:
            return "none";
        case parse_url:
            return "parse_url";
        case resolve:
            return "resolve";
        case connect:
            return "connect";
        case tls_handshake:
            return "tls_handshake";
        case write_request:
            return "write_request";
        case read_response:
            return "read_response";
        case redirect:
            return "redirect";
        case retry_wait:
            return "retry_wait";
        case shutdown:
            return "shutdown";
    }
    return "unknown";
}

std::optional<std::string_view> Response::header(std::string_view name) const noexcept {
    for (const auto& [key, value] : headers) {
        if (key.size() != name.size()) {
            continue;
        }
        bool equal = true;
        for (std::size_t index = 0; index < key.size(); ++index) {
            const auto left = static_cast<unsigned char>(key[index]);
            const auto right = static_cast<unsigned char>(name[index]);
            if (std::tolower(left) != std::tolower(right)) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return value;
        }
    }
    return std::nullopt;
}

}  // namespace steady_http
