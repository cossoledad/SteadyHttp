#include <iostream>
#include <steady_http/client.hpp>
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: sync_download URL\n";
        return 2;
    }
    steady_http::Client client;
    auto result = client.download({.url = argv[1]});
    if (!result) {
        std::cerr << result.error().message << '\n';
        return 1;
    }
    std::cout.write(reinterpret_cast<const char*>(result.value().body.data()),
                    static_cast<std::streamsize>(result.value().body.size()));
}
