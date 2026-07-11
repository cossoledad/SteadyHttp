#include <openssl/ssl.h>

#include <algorithm>
#include <atomic>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/url.hpp>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <random>
#include <steady_http/client.hpp>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "url.hpp"

namespace steady_http {

struct RequestHandle::State {
    std::atomic_bool cancelled{false};
    std::mutex mutex;
    std::function<void()> cancel_active;
};

void RequestHandle::cancel() const noexcept {
    if (const auto state = state_.lock()) {
        state->cancelled.store(true);
        std::lock_guard lock(state->mutex);
        if (state->cancel_active) state->cancel_active();
    }
}
bool RequestHandle::valid() const noexcept { return !state_.expired(); }

namespace {
using Clock = std::chrono::steady_clock;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

Error failure(ErrorCode code, TransferStage stage, std::string message,
              const boost::system::error_code& ec = {}) {
    return Error{.code = code,
                 .stage = stage,
                 .message = std::move(message),
                 .system_error = ec,
                 .http_status = std::nullopt,
                 .attempt = 0,
                 .redirect_count = 0,
                 .retryable = false,
                 .request_may_have_been_processed = false};
}

bool idempotent(Method method) {
    return method == Method::get || method == Method::head || method == Method::put ||
           method == Method::delete_;
}

http::verb verb(Method method) {
    switch (method) {
        case Method::get:
            return http::verb::get;
        case Method::head:
            return http::verb::head;
        case Method::post:
            return http::verb::post;
        case Method::put:
            return http::verb::put;
        case Method::patch:
            return http::verb::patch;
        case Method::delete_:
            return http::verb::delete_;
    }
    return http::verb::unknown;
}

std::chrono::milliseconds remaining(Clock::time_point deadline, std::chrono::milliseconds stage) {
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    return std::max(std::chrono::milliseconds{1}, std::min(stage, left));
}

Response convert(http::response<http::vector_body<std::uint8_t>>&& input) {
    Response output{.status_code = input.result_int(),
                    .reason = std::string(input.reason()),
                    .headers = {},
                    .body = std::move(input.body())};
    for (const auto& field : input.base())
        output.headers.emplace_back(field.name_string(), field.value());
    return output;
}

template <class Stream>
Result<Response> exchange(Stream& stream, const detail::ParsedUrl& url, Method method,
                          const Headers& headers, const ByteVector& body,
                          std::string_view content_type, const RequestOptions& options,
                          std::string_view user_agent, Clock::time_point deadline) {
    http::request<http::vector_body<std::uint8_t>> request{verb(method), url.target, 11};
    try {
        request.set(http::field::host, url.host_header);
        request.set(http::field::connection, "close");
        const bool custom_user_agent = std::ranges::any_of(
            headers, [](const auto& header) { return beast::iequals(header.first, "User-Agent"); });
        if (!user_agent.empty() && !custom_user_agent)
            request.set(http::field::user_agent, user_agent);
        if (!content_type.empty()) request.set(http::field::content_type, content_type);
        for (const auto& [name, value] : headers) request.insert(name, value);
        request.body() = body;
        request.prepare_payload();
    } catch (const std::exception& error) {
        return failure(ErrorCode::invalid_argument, TransferStage::write_request,
                       std::string{"invalid HTTP request: "} + error.what());
    }
    beast::get_lowest_layer(stream).expires_after(remaining(deadline, options.timeouts.write));
    boost::system::error_code ec;
    http::write(stream, request, ec);
    if (ec)
        return failure(ec == beast::error::timeout ? ErrorCode::timeout : ErrorCode::write_failed,
                       TransferStage::write_request, "HTTP request write failed", ec);

    beast::flat_buffer buffer;
    http::response_parser<http::vector_body<std::uint8_t>> parser;
    parser.body_limit(options.max_response_size);
    beast::get_lowest_layer(stream).expires_after(remaining(deadline, options.timeouts.read));
    http::read(stream, buffer, parser, ec);
    if (ec == http::error::body_limit)
        return failure(ErrorCode::response_too_large, TransferStage::read_response,
                       "response body exceeds configured limit", ec);
    if (ec)
        return failure(ec == beast::error::timeout ? ErrorCode::timeout : ErrorCode::read_failed,
                       TransferStage::read_response, "HTTP response read failed", ec);
    return convert(parser.release());
}

Result<Response> once(const std::string& url_text, Method method, const Headers& headers,
                      const ByteVector& body, std::string_view content_type,
                      const RequestOptions& options, const ClientOptions& client,
                      const std::shared_ptr<RequestHandle::State>& state,
                      Clock::time_point deadline) {
    struct ClearActive {
        std::shared_ptr<RequestHandle::State> state;
        ~ClearActive() {
            std::lock_guard lock(state->mutex);
            state->cancel_active = {};
        }
    } clear_active{state};
    if (state->cancelled)
        return failure(ErrorCode::cancelled, TransferStage::none, "request cancelled");
    if (Clock::now() >= deadline)
        return failure(ErrorCode::timeout, TransferStage::none, "total timeout expired");
    auto parsed = detail::parse_url(url_text);
    if (!parsed) return parsed.error();
    try {
        asio::io_context context;
        tcp::resolver resolver(context);
        {
            std::lock_guard lock(state->mutex);
            state->cancel_active = [&resolver] { resolver.cancel(); };
        }
        boost::system::error_code ec;
        std::optional<tcp::resolver::results_type> resolved;
        bool resolve_done = false;
        resolver.async_resolve(parsed.value().host, parsed.value().port,
                               [&](const boost::system::error_code& resolve_error,
                                   tcp::resolver::results_type results) {
                                   ec = resolve_error;
                                   resolved.emplace(std::move(results));
                                   resolve_done = true;
                               });
        context.run_for(remaining(deadline, options.timeouts.resolve));
        if (!resolve_done) {
            resolver.cancel();
            context.restart();
            context.run();
            if (state->cancelled)
                return failure(ErrorCode::cancelled, TransferStage::resolve, "request cancelled");
            return failure(ErrorCode::timeout, TransferStage::resolve, "DNS resolution timed out",
                           beast::error::timeout);
        }
        if (state->cancelled)
            return failure(ErrorCode::cancelled, TransferStage::resolve, "request cancelled");
        if (ec)
            return failure(ErrorCode::resolve_failed, TransferStage::resolve,
                           "DNS resolution failed", ec);
        auto endpoints = std::move(*resolved);

        if (parsed.value().scheme == detail::Scheme::http) {
            beast::tcp_stream stream(context);
            {
                std::lock_guard lock(state->mutex);
                state->cancel_active = [&stream] {
                    boost::system::error_code ignored;
                    stream.socket().close(ignored);
                };
            }
            stream.expires_after(remaining(deadline, options.timeouts.connect));
            stream.connect(endpoints, ec);
            if (state->cancelled)
                return failure(ErrorCode::cancelled, TransferStage::connect, "request cancelled");
            if (ec)
                return failure(
                    ec == beast::error::timeout ? ErrorCode::timeout : ErrorCode::connect_failed,
                    TransferStage::connect, "TCP connection failed", ec);
            return exchange(stream, parsed.value(), method, headers, body, content_type, options,
                            client.user_agent, deadline);
        }

        asio::ssl::context ssl_context(asio::ssl::context::tls_client);
        if (client.verify_peer) {
            ssl_context.set_default_verify_paths();
            if (client.ca_file) ssl_context.load_verify_file(client.ca_file->string());
            if (client.ca_path) ssl_context.add_verify_path(client.ca_path->string());
        }
        beast::ssl_stream<beast::tcp_stream> stream(context, ssl_context);
        stream.set_verify_mode(client.verify_peer ? asio::ssl::verify_peer
                                                  : asio::ssl::verify_none);
        if (client.verify_peer && client.verify_hostname)
            stream.set_verify_callback(asio::ssl::host_name_verification(parsed.value().host));
        if (!SSL_set_tlsext_host_name(stream.native_handle(), parsed.value().host.c_str()))
            return failure(ErrorCode::tls_configuration_failed, TransferStage::tls_handshake,
                           "failed to configure TLS SNI");
        {
            std::lock_guard lock(state->mutex);
            state->cancel_active = [&stream] {
                boost::system::error_code ignored;
                beast::get_lowest_layer(stream).socket().close(ignored);
            };
        }
        beast::get_lowest_layer(stream).expires_after(
            remaining(deadline, options.timeouts.connect));
        beast::get_lowest_layer(stream).connect(endpoints, ec);
        if (ec)
            return failure(
                ec == beast::error::timeout ? ErrorCode::timeout : ErrorCode::connect_failed,
                TransferStage::connect, "TCP connection failed", ec);
        beast::get_lowest_layer(stream).expires_after(
            remaining(deadline, options.timeouts.tls_handshake));
        stream.handshake(asio::ssl::stream_base::client, ec);
        if (state->cancelled)
            return failure(ErrorCode::cancelled, TransferStage::tls_handshake, "request cancelled");
        if (ec) {
            const auto verification = SSL_get_verify_result(stream.native_handle());
            if (client.verify_peer && verification != X509_V_OK) {
                return failure(ErrorCode::certificate_verification_failed,
                               TransferStage::tls_handshake,
                               std::string{"TLS certificate verification failed: "} +
                                   X509_verify_cert_error_string(verification),
                               ec);
            }
            return failure(ErrorCode::tls_handshake_failed, TransferStage::tls_handshake,
                           "TLS handshake failed", ec);
        }
        return exchange(stream, parsed.value(), method, headers, body, content_type, options,
                        client.user_agent, deadline);
    } catch (const std::exception& e) {
        return failure(ErrorCode::tls_configuration_failed, TransferStage::tls_handshake, e.what());
    }
}

bool status_retryable(unsigned status, const RetryOptions& retry) {
    return (status == 408 && retry.retry_http_408) || (status == 429 && retry.retry_http_429) ||
           (status >= 500 && status <= 599 && retry.retry_http_5xx);
}

std::optional<std::chrono::milliseconds> retry_after(const Response& response) {
    const auto value = response.header("Retry-After");
    if (!value || value->empty()) return std::nullopt;
    std::uint64_t seconds = 0;
    const auto [end, error] =
        std::from_chars(value->data(), value->data() + value->size(), seconds);
    if (error != std::errc{} || end != value->data() + value->size()) return std::nullopt;
    constexpr auto maximum_seconds =
        static_cast<std::uint64_t>(std::chrono::milliseconds::max().count() / 1000);
    if (seconds > maximum_seconds) return std::chrono::milliseconds::max();
    return std::chrono::milliseconds{seconds * 1000};
}

Result<Response> run(std::string url, Method method, Headers headers, const ByteVector& body,
                     std::string_view content_type, const RequestOptions& options,
                     const ClientOptions& client,
                     const std::shared_ptr<RequestHandle::State>& state) {
    if (options.timeouts.total <= std::chrono::milliseconds::zero() ||
        options.timeouts.resolve <= std::chrono::milliseconds::zero() ||
        options.timeouts.connect <= std::chrono::milliseconds::zero() ||
        options.timeouts.tls_handshake <= std::chrono::milliseconds::zero() ||
        options.timeouts.write <= std::chrono::milliseconds::zero() ||
        options.timeouts.read <= std::chrono::milliseconds::zero() ||
        options.retries.max_attempts == 0 || options.retries.multiplier < 1.0 ||
        options.retries.jitter_ratio < 0.0 || options.retries.jitter_ratio > 1.0 ||
        options.max_response_size == 0) {
        return failure(ErrorCode::invalid_argument, TransferStage::none,
                       "timeouts, attempts, retry parameters, and response limit are invalid");
    }
    const auto deadline = Clock::now() + options.timeouts.total;
    std::mt19937_64 random{std::random_device{}()};
    std::size_t redirects = 0;
    std::unordered_set<std::string> visited;
    while (true) {
        if (!visited.insert(url).second)
            return failure(ErrorCode::too_many_redirects, TransferStage::redirect,
                           "redirect loop detected");
        Result<Response> result =
            failure(ErrorCode::internal_error, TransferStage::none, "no attempt");
        std::size_t last_attempt = 0;
        for (std::size_t attempt = 1; attempt <= options.retries.max_attempts; ++attempt) {
            last_attempt = attempt;
            result =
                once(url, method, headers, body, content_type, options, client, state, deadline);
            if (state->cancelled)
                return failure(ErrorCode::cancelled, TransferStage::none, "request cancelled");
            const bool retry_method = idempotent(method) || options.retries.retry_non_idempotent;
            bool retry = false;
            if (result)
                retry = status_retryable(result.value().status_code, options.retries);
            else {
                auto& error = result.error();
                error.attempt = attempt;
                error.request_may_have_been_processed =
                    error.stage == TransferStage::read_response ||
                    error.stage == TransferStage::shutdown;
                retry = retry_method &&
                        ((error.code == ErrorCode::timeout && options.retries.retry_timeouts) ||
                         ((result.error().code == ErrorCode::resolve_failed ||
                           result.error().code == ErrorCode::connect_failed ||
                           result.error().code == ErrorCode::write_failed ||
                           result.error().code == ErrorCode::read_failed) &&
                          options.retries.retry_connection_errors));
                error.retryable = retry;
            }
            if (!retry || !retry_method || attempt == options.retries.max_attempts) break;
            const double maximum_delay = static_cast<double>(options.retries.maximum_delay.count());
            const double initial_delay = static_cast<double>(options.retries.initial_delay.count());
            const double base =
                std::min(maximum_delay, initial_delay * std::pow(options.retries.multiplier,
                                                                 static_cast<double>(attempt - 1)));
            std::uniform_real_distribution<double> jitter(-options.retries.jitter_ratio,
                                                          options.retries.jitter_ratio);
            auto delay = std::chrono::milliseconds{
                static_cast<long long>(std::max(0.0, base * (1.0 + jitter(random))))};
            if (result &&
                (result.value().status_code == 429 || result.value().status_code == 503)) {
                if (const auto server_delay = retry_after(result.value())) delay = *server_delay;
            }
            if (Clock::now() + delay >= deadline)
                return failure(ErrorCode::timeout, TransferStage::retry_wait,
                               "total timeout expired during retry wait");
            for (auto end = Clock::now() + delay; Clock::now() < end && !state->cancelled;)
                std::this_thread::sleep_for(std::min(
                    std::chrono::milliseconds{20},
                    std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now())));
            if (state->cancelled)
                return failure(ErrorCode::cancelled, TransferStage::retry_wait,
                               "request cancelled");
        }
        if (!result) return result;
        auto& response = result.value();
        const bool redirect_status = response.status_code == 301 || response.status_code == 302 ||
                                     response.status_code == 303 || response.status_code == 307 ||
                                     response.status_code == 308;
        if (!redirect_status || method != Method::get || !options.redirects.enabled) {
            if (options.treat_non_2xx_as_error &&
                (response.status_code < 200 || response.status_code >= 300))
                return Error{.code = ErrorCode::http_status_error,
                             .stage = TransferStage::read_response,
                             .message = "non-success HTTP status",
                             .system_error = {},
                             .http_status = response.status_code,
                             .attempt = last_attempt,
                             .redirect_count = redirects,
                             .retryable = false,
                             .request_may_have_been_processed = true};
            return result;
        }
        if (++redirects > options.redirects.max_redirects)
            return failure(ErrorCode::too_many_redirects, TransferStage::redirect,
                           "redirect limit exceeded");
        const auto location = response.header("Location");
        if (!location)
            return failure(ErrorCode::redirect_missing_location, TransferStage::redirect,
                           "redirect has no Location header");
        auto base = boost::urls::parse_uri(url);
        auto ref = boost::urls::parse_uri_reference(*location);
        if (!base || !ref)
            return failure(ErrorCode::invalid_url, TransferStage::redirect, "invalid redirect URL");
        const std::string old_host{base->host()};
        boost::urls::url next(base.value());
        next.resolve(ref.value());
        url = next.buffer();
        if (next.host() != old_host) {
            std::erase_if(headers, [](const auto& h) {
                return boost::beast::iequals(h.first, "Authorization") ||
                       boost::beast::iequals(h.first, "Proxy-Authorization") ||
                       boost::beast::iequals(h.first, "Cookie");
            });
        }
    }
}
}  // namespace

class Client::Impl : public std::enable_shared_from_this<Client::Impl> {
   public:
    explicit Impl(ClientOptions value) : options(std::move(value)) {}
    void start() {
        const auto count = std::max<std::size_t>(1, options.worker_threads);
        workers.reserve(count);
        try {
            for (std::size_t index = 0; index < count; ++index) {
                workers.emplace_back([self = shared_from_this()] { self->worker_loop(); });
            }
        } catch (...) {
            stopped = true;
            wake.notify_all();
            for (auto& worker : workers)
                if (worker.joinable()) worker.join();
            workers.clear();
            throw;
        }
    }
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopped || !tasks.empty(); });
                if (tasks.empty()) {
                    if (stopped) return;
                    continue;
                }
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            try {
                task();
            } catch (...) {
            }
        }
    }
    ClientOptions options;
    std::atomic_bool stopped{false};
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<std::shared_ptr<RequestHandle::State>> requests;
    std::deque<std::function<void()>> tasks;
    std::vector<std::thread> workers;
};

Client::Client(ClientOptions options) : impl_(std::make_shared<Impl>(std::move(options))) {
    impl_->start();
}
Client::~Client() { stop(); }
Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&& other) noexcept {
    if (this != &other) {
        stop();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

Result<Response> Client::download(const DownloadRequest& request) {
    const auto impl = impl_;
    if (!impl) return failure(ErrorCode::client_stopped, TransferStage::none, "client stopped");
    const auto state = std::make_shared<RequestHandle::State>();
    {
        std::lock_guard lock(impl->mutex);
        if (impl->stopped)
            return failure(ErrorCode::client_stopped, TransferStage::none, "client stopped");
        impl->requests.push_back(state);
    }
    auto result = run(request.url, Method::get, request.headers, {}, {}, request.options,
                      impl->options, state);
    {
        std::lock_guard lock(impl->mutex);
        std::erase(impl->requests, state);
    }
    return result;
}
Result<Response> Client::upload(UploadRequest request) {
    const auto impl = impl_;
    if (!impl) return failure(ErrorCode::client_stopped, TransferStage::none, "client stopped");
    if (request.method != Method::put && request.method != Method::post)
        return failure(ErrorCode::invalid_argument, TransferStage::none,
                       "upload method must be PUT or POST");
    const auto state = std::make_shared<RequestHandle::State>();
    {
        std::lock_guard lock(impl->mutex);
        if (impl->stopped)
            return failure(ErrorCode::client_stopped, TransferStage::none, "client stopped");
        impl->requests.push_back(state);
    }
    auto result = run(request.url, request.method, std::move(request.headers), request.body,
                      request.content_type, request.options, impl->options, state);
    {
        std::lock_guard lock(impl->mutex);
        std::erase(impl->requests, state);
    }
    return result;
}

RequestHandle Client::async_download(DownloadRequest request, CompletionHandler handler) {
    auto state = std::make_shared<RequestHandle::State>();
    if (!impl_) {
        try {
            handler(failure(ErrorCode::client_stopped, TransferStage::none, "client stopped"));
        } catch (...) {
        }
        return {};
    }
    auto impl = impl_;
    std::unique_lock submission_lock(impl->mutex);
    if (impl->stopped) {
        submission_lock.unlock();
        try {
            handler(failure(ErrorCode::client_stopped, TransferStage::none, "client stopped"));
        } catch (...) {
        }
        return {};
    }
    impl->requests.push_back(state);
    impl->tasks.emplace_back(
        [impl, state, request = std::move(request), handler = std::move(handler)]() mutable {
            auto result = run(request.url, Method::get, std::move(request.headers), {}, {},
                              request.options, impl->options, state);
            {
                std::lock_guard completion_lock(impl->mutex);
                std::erase(impl->requests, state);
            }
            try {
                handler(std::move(result));
            } catch (...) {
            }
        });
    impl->wake.notify_one();
    return RequestHandle{state};
}
RequestHandle Client::async_upload(UploadRequest request, CompletionHandler handler) {
    auto state = std::make_shared<RequestHandle::State>();
    if (!impl_) {
        try {
            handler(failure(ErrorCode::client_stopped, TransferStage::none, "client stopped"));
        } catch (...) {
        }
        return {};
    }
    auto impl = impl_;
    std::unique_lock submission_lock(impl->mutex);
    if (impl->stopped) {
        submission_lock.unlock();
        try {
            handler(failure(ErrorCode::client_stopped, TransferStage::none, "client stopped"));
        } catch (...) {
        }
        return {};
    }
    impl->requests.push_back(state);
    impl->tasks.emplace_back(
        [impl, state, request = std::move(request), handler = std::move(handler)]() mutable {
            Result<Response> result =
                request.method == Method::put || request.method == Method::post
                    ? run(request.url, request.method, std::move(request.headers), request.body,
                          request.content_type, request.options, impl->options, state)
                    : Result<Response>{failure(ErrorCode::invalid_argument, TransferStage::none,
                                               "upload method must be PUT or POST")};
            {
                std::lock_guard completion_lock(impl->mutex);
                std::erase(impl->requests, state);
            }
            try {
                handler(std::move(result));
            } catch (...) {
            }
        });
    impl->wake.notify_one();
    return RequestHandle{state};
}
void Client::stop() {
    const auto impl = impl_;
    if (!impl || impl->stopped.exchange(true)) return;
    std::vector<std::thread> workers;
    {
        std::lock_guard lock(impl->mutex);
        for (const auto& request : impl->requests) RequestHandle{request}.cancel();
        workers.swap(impl->workers);
    }
    impl->wake.notify_all();
    const auto current = std::this_thread::get_id();
    for (auto& thread : workers) {
        if (!thread.joinable()) continue;
        if (thread.get_id() == current)
            thread.detach();
        else
            thread.join();
    }
}
}  // namespace steady_http
