#include <steady_http/client.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
steady_http::ByteVector read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input file: " + path.string());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
}

int main(int argc, char** argv) try {
    if (argc != 3) {
        std::cerr << "usage: steady-http-tester FILE UPLOAD_URL\n"
                     "example: steady-http-tester ./abc.txt http://127.0.0.1:18080/abc.txt\n";
        return 2;
    }
    const auto source = read_file(argv[1]);
    const std::string url = argv[2];
    steady_http::Client client;

    steady_http::UploadRequest request{.url = url,
                                       .method = steady_http::Method::put,
                                       .body = source};
    auto upload = client.upload(std::move(request));
    if (!upload) {
        std::cerr << "upload failed: " << upload.error().message << '\n';
        return 3;
    }
    auto download = client.download({.url = url});
    if (!download) {
        std::cerr << "download failed: " << download.error().message << '\n';
        return 4;
    }
    if (source.size() != download.value().body.size()) {
        std::cerr << "size mismatch: source=" << source.size()
                  << ", downloaded=" << download.value().body.size() << '\n';
        return 5;
    }
    std::cout << "PASS: uploaded and downloaded " << source.size() << " bytes via " << url << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
