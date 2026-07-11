# SteadyHttpTester

Independent consumer of the published `steady-http/0.1.0` Conan package. It
uploads a selected file, downloads the same URL, and compares the two sizes.

```bash
cd TestModule/SteadyHttpTester
invoke configure --build-type=Debug --remote=radxa-conan-pr
invoke build --build-type=Debug
```

After `HttpTempFileServer` is running, execute as many files as needed:

```bash
invoke test --file=/path/abc.txt \
  --upload-url=http://127.0.0.1:18080/abc.txt --build-type=Debug

invoke test --file=/path/video.bin \
  --upload-url=http://127.0.0.1:18080/video.bin --build-type=Debug
```
