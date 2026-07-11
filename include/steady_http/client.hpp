#pragma once
#include <functional>
#include <memory>
#include <steady_http/options.hpp>
#include <steady_http/request.hpp>
#include <steady_http/response.hpp>
#include <steady_http/result.hpp>
#include <steady_http/version.hpp>
namespace steady_http {
/** Completion receives exactly one response or structured failure. */
using CompletionHandler = std::function<void(Result<Response>)>;

class RequestHandle {
   public:
    struct State;
    RequestHandle() noexcept = default;
    void cancel() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

   private:
    explicit RequestHandle(std::weak_ptr<State> state) noexcept : state_(std::move(state)) {}
    std::weak_ptr<State> state_;
    friend class Client;
};

class Client final {
   public:
    explicit Client(ClientOptions = {});
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;

    [[nodiscard]] Result<Response> download(const DownloadRequest& request);
    [[nodiscard]] Result<Response> upload(UploadRequest request);
    [[nodiscard]] RequestHandle async_download(DownloadRequest request, CompletionHandler handler);
    [[nodiscard]] RequestHandle async_upload(UploadRequest request, CompletionHandler handler);
    void stop();

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace steady_http
