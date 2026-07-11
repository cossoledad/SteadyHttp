#!/usr/bin/env python3
"""Temporary HTTP PUT/POST upload and GET download file server."""

import argparse
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading
import time
import tempfile
import ssl
from urllib.parse import unquote, urlsplit


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--folder", default="steady-http-files",
                        help="folder name created directly below /tmp")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--protocol", choices=("http", "https"), default="http")
    parser.add_argument("--cert-file", type=Path)
    parser.add_argument("--key-file", type=Path)
    parser.add_argument("--chunk-delay-ms", type=float, default=0.0,
                        help="optional delay after each 1 MiB chunk for interruption tests")
    return parser.parse_args()


def requested_name(raw_path):
    path = unquote(urlsplit(raw_path).path)
    if not path.startswith("/") or path == "/":
        return None
    relative = path[1:]
    if "/" in relative or relative in {".", ".."} or Path(relative).name != relative:
        return None
    return relative


def size_text(byte_count):
    return f"{byte_count} B ({byte_count / (1024 * 1024):.2f} MiB)"


class TransferLog:
    def __init__(self, operation, name, total, client):
        self.operation = operation
        self.name = name
        self.total = total
        self.client = client
        self.started = time.monotonic()
        self.last_report = self.started
        self.last_bytes = 0
        self.report(0, force=True)

    def report(self, transferred, force=False):
        now = time.monotonic()
        if not force and now - self.last_report < 1.0 and transferred != self.total:
            return
        elapsed = max(now - self.started, 1e-9)
        interval = max(now - self.last_report, 1e-9)
        average = transferred / elapsed / (1024 * 1024)
        current = (transferred - self.last_bytes) / interval / (1024 * 1024)
        percent = 100.0 if self.total == 0 else transferred * 100.0 / self.total
        stamp = datetime.now().astimezone().isoformat(timespec="seconds")
        print(
            f"[{stamp}] [{threading.current_thread().name}] {self.client} "
            f"{self.operation} {self.name!r}: {size_text(transferred)} / "
            f"{size_text(self.total)} ({percent:.1f}%), "
            f"current={current:.2f} MiB/s average={average:.2f} MiB/s "
            f"elapsed={elapsed:.2f}s",
            flush=True,
        )
        self.last_report = now
        self.last_bytes = transferred

    def failed(self, transferred, error):
        elapsed = time.monotonic() - self.started
        print(
            f"{self.operation} {self.name!r} FAILED after {size_text(transferred)}, "
            f"elapsed={elapsed:.2f}s: {type(error).__name__}: {error}",
            flush=True,
        )


def make_handler(storage, chunk_delay):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):
            name = requested_name(self.path)
            source = storage / name if name else None
            if source is None or not source.is_file():
                self.send_error(404, "file not found")
                return
            transferred = 0
            total = source.stat().st_size
            progress = TransferLog("DOWNLOAD", name, total, self.client_address[0])
            try:
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(total))
                self.end_headers()
                with source.open("rb") as stream:
                    while chunk := stream.read(1024 * 1024):
                        self.wfile.write(chunk)
                        transferred += len(chunk)
                        if transferred != total:
                            progress.report(transferred)
                        if chunk_delay:
                            time.sleep(chunk_delay)
                progress.report(transferred, force=True)
            except (OSError, BrokenPipeError, ConnectionError) as error:
                progress.failed(transferred, error)

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

            destination = storage / name
            total = remaining
            transferred = 0
            progress = TransferLog("UPLOAD", name, total, self.client_address[0])
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(
                    mode="wb", prefix=f".{name}.", suffix=".uploading",
                    dir=storage, delete=False) as output:
                    temporary = Path(output.name)
                    while remaining:
                        chunk = self.rfile.read(min(remaining, 1024 * 1024))
                        if not chunk:
                            raise OSError("request body ended early")
                        output.write(chunk)
                        remaining -= len(chunk)
                        transferred += len(chunk)
                        if transferred != total:
                            progress.report(transferred)
                        if chunk_delay:
                            time.sleep(chunk_delay)
                temporary.replace(destination)
            except OSError as error:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
                progress.failed(transferred, error)
                try:
                    self.send_error(500, str(error))
                except (BrokenPipeError, ConnectionError):
                    pass
                return

            progress.report(transferred, force=True)

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
    if args.chunk_delay_ms < 0:
        raise SystemExit("--chunk-delay-ms cannot be negative")
    server = ThreadingHTTPServer(
        (args.host, args.port), make_handler(storage, args.chunk_delay_ms / 1000.0))
    server.daemon_threads = True
    if args.protocol == "https":
        if not args.cert_file or not args.key_file:
            raise SystemExit("HTTPS requires --cert-file and --key-file")
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(args.cert_file.expanduser(), args.key_file.expanduser())
        server.socket = context.wrap_socket(server.socket, server_side=True)
    print(f"storage: {storage}", flush=True)
    print(f"listening: {args.protocol}://{args.host}:{server.server_port}/<file-name>", flush=True)
    print(f"chunk delay: {args.chunk_delay_ms:.1f} ms", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
