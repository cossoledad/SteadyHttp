#!/usr/bin/env python3
"""Temporary HTTP PUT/POST upload and GET download file server."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlsplit


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--folder", default="steady-http-files",
                        help="folder name created directly below /tmp")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    return parser.parse_args()


def requested_name(raw_path):
    path = unquote(urlsplit(raw_path).path)
    if not path.startswith("/") or path == "/":
        return None
    relative = path[1:]
    if "/" in relative or relative in {".", ".."} or Path(relative).name != relative:
        return None
    return relative


def make_handler(storage):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):
            name = requested_name(self.path)
            source = storage / name if name else None
            if source is None or not source.is_file():
                self.send_error(404, "file not found")
                return
            try:
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(source.stat().st_size))
                self.end_headers()
                with source.open("rb") as stream:
                    while chunk := stream.read(1024 * 1024):
                        self.wfile.write(chunk)
            except (OSError, BrokenPipeError) as error:
                print(f"download {name!r} failed: {error}", flush=True)

        def do_PUT(self):
            self._upload()

        def do_POST(self):
            self._upload()

        def _upload(self):
            name = requested_name(self.path)
            if not name:
                self.send_error(400, "URL must contain one file name, for example /abc.txt")
                return
            try:
                remaining = int(self.headers["Content-Length"])
                if remaining < 0:
                    raise ValueError
            except (KeyError, TypeError, ValueError):
                self.send_error(400, "valid Content-Length is required")
                return

            temporary = storage / f".{name}.uploading"
            destination = storage / name
            try:
                with temporary.open("wb") as output:
                    while remaining:
                        chunk = self.rfile.read(min(remaining, 1024 * 1024))
                        if not chunk:
                            raise OSError("request body ended early")
                        output.write(chunk)
                        remaining -= len(chunk)
                temporary.replace(destination)
            except OSError as error:
                temporary.unlink(missing_ok=True)
                self.send_error(500, str(error))
                return

            self.send_response(201)
            self.send_header("Location", f"/{name}")
            self.send_header("Content-Length", "0")
            self.end_headers()

        def log_message(self, fmt, *args):
            print(f"{self.client_address[0]} - {fmt % args}", flush=True)

    return Handler


def main():
    args = arguments()
    if not args.folder or Path(args.folder).name != args.folder or args.folder in {".", ".."}:
        raise SystemExit("--folder must be one directory name below /tmp")
    storage = Path("/tmp") / args.folder
    storage.mkdir(mode=0o700, parents=True, exist_ok=True)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(storage))
    print(f"storage: {storage}", flush=True)
    print(f"listening: http://{args.host}:{server.server_port}/<file-name>", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
