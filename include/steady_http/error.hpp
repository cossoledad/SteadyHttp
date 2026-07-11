#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <system_error>

namespace steady_http {

enum class ErrorCode {
    invalid_url,
    unsupported_scheme,
    resolve_failed,
    connect_failed,
    tls_configuration_failed,
    tls_handshake_failed,
    certificate_verification_failed,
    write_failed,
    read_failed,
    timeout,
    cancelled,
    response_too_large,
    invalid_response,
    too_many_redirects,
    redirect_missing_location,
    http_status_error,
    retry_exhausted,
    client_stopped,
    invalid_argument,
    internal_error,
};

enum class TransferStage {
    none,
    parse_url,
    resolve,
    connect,
    tls_handshake,
    write_request,
    read_response,
    redirect,
    retry_wait,
    shutdown,
};

struct Error {
    ErrorCode code{ErrorCode::internal_error};
    TransferStage stage{TransferStage::none};
    std::string message;
    std::error_code system_error;
    std::optional<unsigned> http_status;
    std::size_t attempt{};
    std::size_t redirect_count{};
    bool retryable{};
    bool request_may_have_been_processed{};
};

}  // namespace steady_http
