#pragma once

#include <stdexcept>
#include <steady_http/error.hpp>
#include <utility>
#include <variant>

namespace steady_http {

/** A value-or-error result. Accessing its inactive alternative throws logic_error. */
template <class T>
class Result final {
   public:
    Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

    [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() & {
        if (!has_value()) {
            throw std::logic_error("Result does not contain a value");
        }
        return std::get<0>(storage_);
    }
    [[nodiscard]] const T& value() const& {
        if (!has_value()) {
            throw std::logic_error("Result does not contain a value");
        }
        return std::get<0>(storage_);
    }
    [[nodiscard]] T&& value() && { return std::move(value()); }

    [[nodiscard]] Error& error() & {
        if (has_value()) {
            throw std::logic_error("Result does not contain an error");
        }
        return std::get<1>(storage_);
    }
    [[nodiscard]] const Error& error() const& {
        if (has_value()) {
            throw std::logic_error("Result does not contain an error");
        }
        return std::get<1>(storage_);
    }

   private:
    std::variant<T, Error> storage_;
};

}  // namespace steady_http
