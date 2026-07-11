#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <steady_http/client.hpp>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;

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
    options.max_response_size =
        source_size == std::numeric_limits<std::size_t>::max() ? source_size : source_size + 1;
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
        std::string detail = std::string(operation) + " failed" +
                             "\n  code: " + std::string(steady_http::to_string(error.code)) +
                             "\n  stage: " + std::string(steady_http::to_string(error.stage)) +
                             "\n  message: " + error.message +
                             "\n  attempt: " + std::to_string(error.attempt) +
                             "\n  redirect_count: " + std::to_string(error.redirect_count) +
                             "\n  retryable: " + (error.retryable ? "true" : "false") +
                             "\n  request_may_have_been_processed: " +
                             (error.request_may_have_been_processed ? "true" : "false");
        if (error.system_error) {
            detail += "\n  system_error: " + error.system_error.message() + " (" +
                      std::to_string(error.system_error.value()) + ")";
        }
        if (error.http_status) detail += "\n  http_status: " + std::to_string(*error.http_status);
        throw std::runtime_error(std::move(detail));
    }
}

void require_size(std::size_t expected, const steady_http::Response& response,
                  std::string_view operation) {
    if (response.body.size() != expected) {
        throw std::runtime_error(std::string(operation) +
                                 " size mismatch: source=" + std::to_string(expected) +
                                 ", downloaded=" + std::to_string(response.body.size()));
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
    const std::filesystem::path source_path = std::filesystem::absolute(argv[1]);
    const auto source_size_value = std::filesystem::file_size(source_path);
    if (source_size_value > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("input file is too large for this process");
    const auto source_size = static_cast<std::size_t>(source_size_value);
    const std::string url = argv[2];
    const auto options = large_file_options(source_size);
    steady_http::Client client;
    std::cout << "source: " << source_path << '\n'
              << "URL: " << url << '\n'
              << "source size: " << source_size << " bytes\n"
              << "memory model: each request and response body is entirely resident in memory\n";

    {
        const auto load_started = Clock::now();
        auto body = read_file(source_path);
        std::cout << "loaded synchronous upload body in " << seconds_since(load_started) << " s\n";
        std::cout << "[1/4] synchronous upload..." << std::flush;
        const auto started = Clock::now();
        steady_http::UploadRequest request{.url = url,
                                           .method = steady_http::Method::put,
                                           .body = std::move(body),
                                           .options = options};
        auto result = client.upload(std::move(request));
        require_success(result, "synchronous upload");
        print_statistics(source_size, seconds_since(started));
    }

    {
        std::cout << "[2/4] synchronous download..." << std::flush;
        const auto started = Clock::now();
        auto result = client.download({.url = url, .options = options});
        require_success(result, "synchronous download");
        require_size(source_size, result.value(), "synchronous download");
        print_statistics(result.value().body.size(), seconds_since(started));
    }

    {
        const auto load_started = Clock::now();
        auto body = read_file(source_path);
        std::cout << "loaded asynchronous upload body in " << seconds_since(load_started) << " s\n";
        std::cout << "[3/4] asynchronous upload..." << std::flush;
        const auto started = Clock::now();
        std::promise<steady_http::Result<steady_http::Response>> promise;
        auto future = promise.get_future();
        steady_http::UploadRequest request{.url = url,
                                           .method = steady_http::Method::put,
                                           .body = std::move(body),
                                           .options = options};
        auto handle = client.async_upload(
            std::move(request), [&promise](auto result) { promise.set_value(std::move(result)); });
        if (!handle.valid())
            throw std::runtime_error("asynchronous upload returned an invalid handle");
        auto result = future.get();
        require_success(result, "asynchronous upload");
        print_statistics(source_size, seconds_since(started));
    }

    {
        std::cout << "[4/4] asynchronous download..." << std::flush;
        const auto started = Clock::now();
        std::promise<steady_http::Result<steady_http::Response>> promise;
        auto future = promise.get_future();
        auto handle = client.async_download(
            {.url = url, .options = options},
            [&promise](auto result) { promise.set_value(std::move(result)); });
        if (!handle.valid())
            throw std::runtime_error("asynchronous download returned an invalid handle");
        auto result = future.get();
        require_success(result, "asynchronous download");
        require_size(source_size, result.value(), "asynchronous download");
        print_statistics(result.value().body.size(), seconds_since(started));
    }

    std::cout << "PASS: all four transfers completed for " << source_size << " bytes via " << url
              << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "\nFAIL: " << error.what() << '\n';
    return 1;
}
