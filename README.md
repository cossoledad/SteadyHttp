# SteadyHttp

SteadyHttp is a C++20 binary HTTP/1.1 client built with Boost.Asio, Beast,
Boost.URL, and OpenSSL. Its public API does not expose those dependencies.

## Features

- HTTP and HTTPS GET, PUT, and POST, including explicit ports and IPv6 URLs
- synchronous and callback-based asynchronous APIs with cancellation
- certificate-chain and hostname verification plus TLS SNI by default
- per-stage and total deadlines, bounded response bodies
- idempotency-aware retry with exponential backoff and jitter
- configurable retry of 408, 429, and 5xx responses
- GET redirects (absolute or relative), loop limits, and removal of credentials
  on cross-host redirects
- structured value-or-error results

Each redirect target receives a fresh retry budget, while all redirects and
attempts share one total deadline. Upload redirects are deliberately not
followed in version 0.1 because changing methods can have side effects.

Request and response bodies are held completely in memory as `ByteVector`.
The API is consequently intended for bounded transfers; streaming is planned
for a later version.

## Example

```cpp
#include <steady_http/client.hpp>

steady_http::Client client;
auto result = client.download({.url = "https://example.com/data.bin"});
if (!result) {
    // result.error() contains code, stage, native error, and HTTP status.
}
```

Uploads move the body into the request:

```cpp
steady_http::UploadRequest request{
    .url = "https://example.com/object",
    .method = steady_http::Method::put,
    .body = {0, 1, 2, 3},
};
auto result = client.upload(std::move(request));
```

For asynchronous calls, keep the returned handle while cancellation is useful:

```cpp
auto handle = client.async_download(
    {.url = "https://example.com/data.bin"},
    [](steady_http::Result<steady_http::Response> result) { /* ... */ });
handle.cancel();
```

Setting `verify_peer` or `verify_hostname` to false is intended only for
isolated tests. Production clients should retain both defaults.

## Build and package

```bash
invoke configure --build-type=Debug
invoke build --build-type=Debug
invoke package --build-type=Release
invoke export --build-type=Release
invoke publish --remote=radxa-conan-pr --build-type=Release
```

CMake installation exports `SteadyHttp::SteadyHttp` for
`find_package(SteadyHttp)`. The Conan reference is `steady-http/0.1.0`.

## Package acceptance

Testing is intentionally separated from the library build:

- [`HttpTempFileServer`](TestModule/HttpTempFileServer/README.md) stores uploaded
  files below `/tmp` and serves each file from the same URL.
- [`SteadyHttpTester`](TestModule/SteadyHttpTester/README.md) is an independent
  Conan consumer that uploads and downloads selected files.

Start the server first:

```bash
cd TestModule/HttpTempFileServer
invoke start --folder=steady-http-files --port=18080
```

Then run the consumer in another terminal:

```bash
cd TestModule/SteadyHttpTester
invoke test --file=/absolute/path/data.bin \
  --upload-url=http://127.0.0.1:18080/data.bin --build-type=Debug
```

## License

MIT. See [LICENSE](LICENSE).
