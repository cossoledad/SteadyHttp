#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace steady_http {

struct TimeoutOptions {
    std::chrono::milliseconds resolve{5000};
    std::chrono::milliseconds connect{5000};
    std::chrono::milliseconds tls_handshake{5000};
    std::chrono::milliseconds write{30000};
    std::chrono::milliseconds read{30000};
    std::chrono::milliseconds total{60000};
};

struct RetryOptions {
    std::size_t max_attempts{3};
    std::chrono::milliseconds initial_delay{200};
    std::chrono::milliseconds maximum_delay{5000};
    double multiplier{2.0};
    double jitter_ratio{0.2};
    bool retry_timeouts{true};
    bool retry_connection_errors{true};
    bool retry_http_408{true};
    bool retry_http_429{true};
    bool retry_http_5xx{true};
    bool retry_non_idempotent{false};
};

struct RedirectOptions {
    bool enabled{true};
    std::size_t max_redirects{5};
};

struct RequestOptions {
    TimeoutOptions timeouts;
    RetryOptions retries;
    RedirectOptions redirects;
    std::size_t max_response_size{256ULL * 1024ULL * 1024ULL};
    bool treat_non_2xx_as_error{true};
};

struct ClientOptions {
    std::optional<std::filesystem::path> ca_file;
    std::optional<std::filesystem::path> ca_path;
    bool verify_peer{true};
    bool verify_hostname{true};
    std::string user_agent{"SteadyHttp/0.1.0"};
    std::size_t worker_threads{1};
};

}  // namespace steady_http
