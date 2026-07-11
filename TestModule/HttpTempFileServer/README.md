# HttpTempFileServer

Standalone Python 3 HTTP server with no third-party dependencies. Uploaded
files are stored in a selected directory directly below `/tmp`.

```bash
cd TestModule/HttpTempFileServer
invoke start --folder=steady-http-files --host=127.0.0.1 --port=18080
```

For any single file name, upload and download use the same URL:

```text
PUT  http://127.0.0.1:18080/abc.txt
POST http://127.0.0.1:18080/abc.txt
GET  http://127.0.0.1:18080/abc.txt
```

The example is stored at `/tmp/steady-http-files/abc.txt`. Uploads use a
temporary file and atomic rename, so downloads never observe partial content.
The server logs byte progress, percentage, elapsed time, and current/average
MiB/s. For a reproducible interruption test, slow each 1 MiB chunk slightly:

```bash
invoke start --folder=steady-http-files --port=18080 --chunk-delay-ms=50
```

Cleanup is explicit:

```bash
invoke clean --folder=steady-http-files
```
