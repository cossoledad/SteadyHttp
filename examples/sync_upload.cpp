#include <iostream>
#include <iterator>
#include <steady_http/client.hpp>
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: sync_upload URL < data\n";
        return 2;
    }
    std::vector<char> input{std::istreambuf_iterator<char>{std::cin}, {}};
    steady_http::UploadRequest request{.url = argv[1]};
    request.body.assign(input.begin(), input.end());
    steady_http::Client client;
    auto result = client.upload(std::move(request));
    if (!result) {
        std::cerr << result.error().message << '\n';
        return 1;
    }
    std::cout << result.value().status_code << '\n';
}
