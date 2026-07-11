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

## HTTPS

Generate a private test CA and a localhost certificate (SANs include
`localhost`, `127.0.0.1`, and `::1`):

```bash
invoke certificates
invoke start --protocol=https --host=127.0.0.1 --port=18443
```

Generated private keys and certificates are placed in `certificates/` and are
ignored by Git. They are strictly for local tests. Custom paths are supported:

```bash
invoke start --protocol=https --port=18443 \
  --cert-file=/path/server-cert.pem --key-file=/path/server-key.pem
```

Cleanup is explicit:

```bash
invoke clean --folder=steady-http-files
```
