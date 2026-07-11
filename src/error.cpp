#include <cctype>
#include <steady_http/response.hpp>

namespace steady_http {

std::optional<std::string_view> Response::header(std::string_view name) const noexcept {
    for (const auto& [key, value] : headers) {
        if (key.size() != name.size()) {
            continue;
        }
        bool equal = true;
        for (std::size_t index = 0; index < key.size(); ++index) {
            const auto left = static_cast<unsigned char>(key[index]);
            const auto right = static_cast<unsigned char>(name[index]);
            if (std::tolower(left) != std::tolower(right)) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return value;
        }
    }
    return std::nullopt;
}

}  // namespace steady_http
