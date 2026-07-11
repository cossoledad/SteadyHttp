#pragma once

#include <cstdint>
#include <steady_http/options.hpp>
#include <string>
#include <utility>
#include <vector>

namespace steady_http {

using ByteVector = std::vector<std::uint8_t>;
using Headers = std::vector<std::pair<std::string, std::string>>;

enum class Method { get, head, post, put, patch, delete_ };

struct DownloadRequest {
    std::string url;
    Headers headers;
    RequestOptions options;
};

struct UploadRequest {
    std::string url;
    Method method{Method::put};
    Headers headers;
    std::string content_type{"application/octet-stream"};
    ByteVector body;
    RequestOptions options;
};

}  // namespace steady_http
