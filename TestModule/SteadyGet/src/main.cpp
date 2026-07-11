#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <steady_http/client.hpp>
#include <string>
#include <string_view>

namespace {
using Clock = std::chrono::steady_clock;

struct Arguments {
    std::string url;
    std::filesystem::path output;
    std::optional<std::filesystem::path> ca_file;
    std::size_t maximum_bytes{1024ULL * 1024ULL * 1024ULL};
    std::chrono::seconds timeout{300};
};

[[noreturn]] void usage(std::string_view message = {}) {
    if (!message.empty()) std::cerr << "error: " << message << "\n\n";
    std::cerr << "usage: steady-get URL [-o FILE] [--max-size-mib N] "
                 "[--timeout-seconds N] [--ca-file FILE]\n";
    throw std::runtime_error("invalid command line");
}

std::uint64_t positive_integer(std::string_view text, std::string_view option) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0)
        usage(std::string(option) + " requires a positive integer");
    return value;
}

std::filesystem::path inferred_name(std::string_view url) {
    const auto query = url.find_first_of("?#");
    url = url.substr(0, query);
    const auto slash = url.find_last_of('/');
    if (slash != std::string_view::npos && slash + 1 < url.size())
        return std::string(url.substr(slash + 1));
    return "download.bin";
}

Arguments parse(int argc, char** argv) {
    if (argc < 2) usage();
    Arguments args;
    args.url = argv[1];
    for (int index = 2; index < argc; ++index) {
        const std::string_view option = argv[index];
        if ((option == "-o" || option == "--output") && index + 1 < argc) {
            args.output = argv[++index];
        } else if (option == "--ca-file" && index + 1 < argc) {
            args.ca_file = std::filesystem::absolute(argv[++index]);
        } else if (option == "--max-size-mib" && index + 1 < argc) {
            constexpr std::uint64_t mib = 1024ULL * 1024ULL;
            const auto count = positive_integer(argv[++index], option);
            if (count > std::numeric_limits<std::size_t>::max() / mib)
                usage("--max-size-mib is too large");
            args.maximum_bytes = static_cast<std::size_t>(count * mib);
        } else if (option == "--timeout-seconds" && index + 1 < argc) {
            const auto count = positive_integer(argv[++index], option);
            if (count > static_cast<std::uint64_t>(std::chrono::seconds::max().count()))
                usage("--timeout-seconds is too large");
            args.timeout = std::chrono::seconds{count};
        } else {
            usage("unknown or incomplete option: " + std::string(option));
        }
    }
    if (args.output.empty()) args.output = inferred_name(args.url);
    return args;
}

void print_error(const steady_http::Error& error) {
    std::cerr << "download failed\n"
              << "  code: " << steady_http::to_string(error.code) << '\n'
              << "  stage: " << steady_http::to_string(error.stage) << '\n'
              << "  message: " << error.message << '\n'
              << "  attempt: " << error.attempt << '\n'
              << "  retryable: " << (error.retryable ? "true" : "false") << '\n';
    if (error.http_status) std::cerr << "  HTTP status: " << *error.http_status << '\n';
    if (error.system_error)
        std::cerr << "  system error: " << error.system_error.message() << " ("
                  << error.system_error.value() << ")\n";
}
}  // namespace

int main(int argc, char** argv) try {
    const auto args = parse(argc, argv);
    if (args.ca_file && !std::filesystem::is_regular_file(*args.ca_file))
        throw std::runtime_error("CA file does not exist: " + args.ca_file->string());

    steady_http::ClientOptions client_options;
    client_options.ca_file = args.ca_file;
    steady_http::Client client{std::move(client_options)};
    steady_http::DownloadRequest request{.url = args.url, .headers = {}, .options = {}};
    request.options.max_response_size = args.maximum_bytes;
    request.options.timeouts.read = args.timeout;
    request.options.timeouts.total = args.timeout;

    std::cout << "URL: " << args.url << '\n'
              << "output: " << std::filesystem::absolute(args.output) << '\n'
              << "maximum response: " << args.maximum_bytes << " bytes\n"
              << "downloading..." << std::flush;
    const auto started = Clock::now();
    auto result = client.download(request);
    const auto seconds = std::chrono::duration<double>(Clock::now() - started).count();
    if (!result) {
        std::cout << " FAILED\n";
        print_error(result.error());
        return 1;
    }

    const auto& response = result.value();
    const auto temporary = args.output.string() + ".part";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot open temporary output: " + temporary);
        if (!response.body.empty())
            output.write(reinterpret_cast<const char*>(response.body.data()),
                         static_cast<std::streamsize>(response.body.size()));
        if (!output) throw std::runtime_error("failed while writing: " + temporary);
    }
    std::filesystem::rename(temporary, args.output);
    const double mib = static_cast<double>(response.body.size()) / (1024.0 * 1024.0);
    std::cout << " DONE\n"
              << "HTTP status: " << response.status_code << ' ' << response.reason << '\n'
              << "received: " << response.body.size() << " bytes\n"
              << "elapsed: " << seconds << " s\n"
              << "average: " << (seconds > 0.0 ? mib / seconds : 0.0) << " MiB/s\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "steady-get: " << error.what() << '\n';
    return 2;
}
