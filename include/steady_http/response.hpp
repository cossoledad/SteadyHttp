#pragma once

#include <optional>
#include <steady_http/request.hpp>
#include <string>
#include <string_view>

namespace steady_http {

struct Response {
    unsigned status_code{};
    std::string reason;
    Headers headers;
    ByteVector body;

    [[nodiscard]] std::optional<std::string_view> header(std::string_view name) const noexcept;
};

}  // namespace steady_http
