#include <steady_http/client.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;

std::string_view code_name(steady_http::ErrorCode code) {
    using enum steady_http::ErrorCode;
    switch (code) {
        case invalid_url: return "invalid_url"; case unsupported_scheme: return "unsupported_scheme";
        case resolve_failed: return "resolve_failed"; case connect_failed: return "connect_failed";
        case tls_configuration_failed: return "tls_configuration_failed";
        case tls_handshake_failed: return "tls_handshake_failed";
        case certificate_verification_failed: return "certificate_verification_failed";
        case write_failed: return "write_failed"; case read_failed: return "read_failed";
        case timeout: return "timeout"; case cancelled: return "cancelled";
        case response_too_large: return "response_too_large";
        case invalid_response: return "invalid_response"; case too_many_redirects: return "too_many_redirects";
        case redirect_missing_location: return "redirect_missing_location";
        case http_status_error: return "http_status_error"; case retry_exhausted: return "retry_exhausted";
        case client_stopped: return "client_stopped"; case invalid_argument: return "invalid_argument";
        case internal_error: return "internal_error";
    }
    return "unknown";
}

std::string_view stage_name(steady_http::TransferStage stage) {
    using enum steady_http::TransferStage;
    switch (stage) {
        case none: return "none"; case parse_url: return "parse_url"; case resolve: return "resolve";
        case connect: return "connect"; case tls_handshake: return "tls_handshake";
        case write_request: return "write_request"; case read_response: return "read_response";
        case redirect: return "redirect"; case retry_wait: return "retry_wait";
        case shutdown: return "shutdown";
    }
    return "unknown";
}

double seconds_since(Clock::time_point started) {
    return std::chrono::duration<double>(Clock::now() - started).count();
}

steady_http::RequestOptions large_file_options(std::size_t source_size) {
    steady_http::RequestOptions options;
    constexpr auto transfer_timeout = std::chrono::minutes{30};
    options.timeouts.write = transfer_timeout;
    options.timeouts.read = transfer_timeout;
    options.timeouts.total = transfer_timeout;
    // One extra byte avoids an edge-condition when the configured limit equals the body size.
    options.max_response_size = source_size == std::numeric_limits<std::size_t>::max()
        ? source_size : source_size + 1;
    return options;
}

void print_statistics(std::size_t bytes, double seconds) {
    const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    const double rate = seconds > 0.0 ? mib / seconds : 0.0;
    std::cout << " PASS | bytes=" << bytes << " (" << mib << " MiB)"
              << " | elapsed=" << seconds << " s | average=" << rate << " MiB/s\n";
}

steady_http::ByteVector read_file(const std::filesystem::path& path) {
    const auto file_size = std::filesystem::file_size(path);
    if (file_size > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("input file is too large for this process");
    steady_http::ByteVector bytes(static_cast<std::size_t>(file_size));
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input file: " + path.string());
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()),
                                      static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("cannot read complete input file: " + path.string());
    return bytes;
}

void require_success(const steady_http::Result<steady_http::Response>& result,
                     std::string_view operation) {
    if (!result) {
        const auto& error = result.error();
        std::string detail = std::string(operation) + " failed"
            + "\n  code: " + std::string(code_name(error.code))
            + "\n  stage: " + std::string(stage_name(error.stage))
            + "\n  message: " + error.message
            + "\n  attempt: " + std::to_string(error.attempt)
            + "\n  redirect_count: " + std::to_string(error.redirect_count)
            + "\n  retryable: " + (error.retryable ? "true" : "false")
            + "\n  request_may_have_been_processed: "
            + (error.request_may_have_been_processed ? "true" : "false");
        if (error.system_error) {
            detail += "\n  system_error: " + error.system_error.message()
                + " (" + std::to_string(error.system_error.value()) + ")";
        }
        if (error.http_status) detail += "\n  http_status: " + std::to_string(*error.http_status);
        throw std::runtime_error(std::move(detail));
    }
}

void require_size(std::size_t expected, const steady_http::Response& response,
                  std::string_view operation) {
    if (response.body.size() != expected) {
        throw std::runtime_error(std::string(operation) + " size mismatch: source=" +
                                 std::to_string(expected) + ", downloaded=" +
                                 std::to_string(response.body.size()));
    }
}
}  // namespace

int main(int argc, char** argv) try {
    if (argc != 3) {
        std::cerr << "usage: steady-http-tester FILE UPLOAD_URL\n"
                     "example: steady-http-tester ./abc.txt http://127.0.0.1:18080/abc.txt\n";
        return 2;
    }
    std::cout.setf(std::ios::fixed);
    std::cout.precision(2);
    const auto load_started = Clock::now();
    const auto source = read_file(argv[1]);
    const auto load_seconds = seconds_since(load_started);
    const std::string url = argv[2];
    const auto options = large_file_options(source.size());
    steady_http::Client client;
    std::cout << "source: " << std::filesystem::absolute(argv[1]) << '\n'
              << "URL: " << url << '\n'
              << "memory model: entire request and response bodies are resident in memory\n"
              << "loaded " << source.size() << " bytes in " << load_seconds << " s ("
              << (static_cast<double>(source.size()) / (1024.0 * 1024.0) / std::max(load_seconds, 1e-9))
              << " MiB/s)\n";

    std::cout << "[1/4] synchronous upload..." << std::flush;
    auto started = Clock::now();
    steady_http::UploadRequest sync_upload_request{
        .url = url, .method = steady_http::Method::put, .body = source, .options = options};
    auto sync_upload = client.upload(std::move(sync_upload_request));
    require_success(sync_upload, "synchronous upload");
    print_statistics(source.size(), seconds_since(started));

    std::cout << "[2/4] synchronous download..." << std::flush;
    started = Clock::now();
    auto sync_download = client.download({.url = url, .options = options});
    require_success(sync_download, "synchronous download");
    require_size(source.size(), sync_download.value(), "synchronous download");
    print_statistics(sync_download.value().body.size(), seconds_since(started));

    std::cout << "[3/4] asynchronous upload..." << std::flush;
    started = Clock::now();
    std::promise<steady_http::Result<steady_http::Response>> upload_promise;
    auto upload_future = upload_promise.get_future();
    steady_http::UploadRequest async_upload_request{
        .url = url, .method = steady_http::Method::put, .body = source, .options = options};
    auto upload_handle = client.async_upload(
        std::move(async_upload_request),
        [&upload_promise](auto result) { upload_promise.set_value(std::move(result)); });
    if (!upload_handle.valid()) throw std::runtime_error("asynchronous upload returned an invalid handle");
    auto async_upload = upload_future.get();
    require_success(async_upload, "asynchronous upload");
    print_statistics(source.size(), seconds_since(started));

    std::cout << "[4/4] asynchronous download..." << std::flush;
    started = Clock::now();
    std::promise<steady_http::Result<steady_http::Response>> download_promise;
    auto download_future = download_promise.get_future();
    auto download_handle = client.async_download(
        {.url = url, .options = options},
        [&download_promise](auto result) { download_promise.set_value(std::move(result)); });
    if (!download_handle.valid()) throw std::runtime_error("asynchronous download returned an invalid handle");
    auto async_download = download_future.get();
    require_success(async_download, "asynchronous download");
    require_size(source.size(), async_download.value(), "asynchronous download");
    print_statistics(async_download.value().body.size(), seconds_since(started));

    std::cout << "PASS: all four transfers completed for " << source.size()
              << " bytes via " << url << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "\nFAIL: " << error.what() << '\n';
    return 1;
}
