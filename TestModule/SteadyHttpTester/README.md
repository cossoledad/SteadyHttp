# SteadyHttpTester

Independent consumer of the published `steady-http/0.1.0` Conan package. It
performs synchronous upload/download and asynchronous upload/download against
the same URL, comparing downloaded sizes with the selected source file.

```bash
cd TestModule/SteadyHttpTester
invoke configure --build-type=Debug --remote=radxa-conan-pr
invoke build --build-type=Debug
invoke graph --build-type=Debug --remote=radxa-conan-pr
```

After `HttpTempFileServer` is running, execute as many files as needed:

```bash
invoke test --file=/path/abc.txt \
  --upload-url=http://127.0.0.1:18080/abc.txt --build-type=Debug

invoke test --file=/path/video.bin \
  --upload-url=http://127.0.0.1:18080/video.bin --build-type=Debug
```

For HTTPS, first verify that the self-signed test hierarchy is rejected when
its CA is not configured:

```bash
invoke test --file=/path/abc.txt \
  --upload-url=https://localhost:18443/abc.txt \
  --expect-tls-failure --build-type=Debug
```

Then trust only the generated test CA and run all four transfers:

```bash
invoke test --file=/path/abc.txt \
  --upload-url=https://localhost:18443/abc.txt \
  --ca-file=../HttpTempFileServer/certificates/ca-cert.pem \
  --build-type=Debug
```

The `graph` command writes HTML, Graphviz DOT, and JSON representations into
`dependency-graph/`. To debug, open this `SteadyHttpTester` directory as the VS
Code workspace, start `HttpTempFileServer`, select **Debug SteadyHttpTester**,
and enter the source file path and URL when prompted.

The tester prints file-load time and per-operation bytes, elapsed time, and
average MiB/s. It raises the response limit to the selected file size and uses
a 30-minute transfer deadline for large-file testing. SteadyHttp 0.1 currently
buffers complete bodies and therefore does not expose live client byte-progress
callbacks; live progress is reported by `HttpTempFileServer`.
