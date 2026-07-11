#!/usr/bin/env python3
"""Generate a local test CA and localhost HTTPS server certificate with OpenSSL."""

import argparse
from pathlib import Path
import subprocess


def run(*command):
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("certificates"))
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    output = args.output.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=True)
    ca_key = output / "ca-key.pem"
    ca_cert = output / "ca-cert.pem"
    server_key = output / "server-key.pem"
    server_csr = output / "server.csr"
    server_cert = output / "server-cert.pem"
    extensions = output / "server-ext.cnf"
    generated = (ca_key, ca_cert, server_key, server_csr, server_cert)
    if not args.force and any(path.exists() for path in generated):
        raise SystemExit(f"certificate files already exist in {output}; use --force to replace them")
    for path in generated:
        path.unlink(missing_ok=True)
    extensions.write_text(
        "basicConstraints=CA:FALSE\n"
        "keyUsage=digitalSignature,keyEncipherment\n"
        "extendedKeyUsage=serverAuth\n"
        "subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1\n",
        encoding="utf-8",
    )
    run("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048",
        "-out", str(ca_key))
    run("openssl", "req", "-x509", "-new", "-sha256", "-days", "3650",
        "-key", str(ca_key), "-subj", "/CN=SteadyHttp Test CA", "-out", str(ca_cert))
    run("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048",
        "-out", str(server_key))
    run("openssl", "req", "-new", "-sha256", "-key", str(server_key),
        "-subj", "/CN=localhost", "-out", str(server_csr))
    run("openssl", "x509", "-req", "-sha256", "-days", "825",
        "-in", str(server_csr), "-CA", str(ca_cert), "-CAkey", str(ca_key),
        "-CAcreateserial", "-extfile", str(extensions), "-out", str(server_cert))
    server_csr.unlink(missing_ok=True)
    (output / "ca-cert.srl").unlink(missing_ok=True)
    ca_key.chmod(0o600)
    server_key.chmod(0o600)
    print(f"CA certificate: {ca_cert}")
    print(f"server certificate: {server_cert}")
    print(f"server private key: {server_key}")


if __name__ == "__main__":
    main()
