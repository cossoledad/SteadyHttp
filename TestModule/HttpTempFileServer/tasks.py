"""Commands for the standalone temporary file server."""
from pathlib import Path
import shlex
import shutil
from invoke import task


@task
def certificates(context, output="certificates", force=False):
    """Generate a test CA and localhost server certificate."""
    command = (f"python3 {shlex.quote(str(Path(__file__).parent / 'generate_certs.py'))} "
               f"--output={shlex.quote(output)}")
    if force:
        command += " --force"
    context.run(command)


@task
def start(context, folder="steady-http-files", host="127.0.0.1", port=18080,
          chunk_delay_ms=0, protocol="http", cert_file=None, key_file=None):
    """Start the server; uploaded files are stored under /tmp/FOLDER."""
    command = (
        f"python3 {shlex.quote(str(Path(__file__).parent / 'server.py'))} "
        f"--folder={shlex.quote(folder)} --host={shlex.quote(host)} --port={int(port)} "
        f"--chunk-delay-ms={float(chunk_delay_ms)} --protocol={shlex.quote(protocol)}"
    )
    if protocol == "https":
        base = Path(__file__).parent / "certificates"
        cert_file = Path(cert_file).expanduser() if cert_file else base / "server-cert.pem"
        key_file = Path(key_file).expanduser() if key_file else base / "server-key.pem"
        command += f" --cert-file={shlex.quote(str(cert_file))} --key-file={shlex.quote(str(key_file))}"
    context.run(command)


@task
def clean(_context, folder="steady-http-files"):
    """Delete the selected temporary storage folder."""
    target = Path("/tmp") / folder
    if target.parent != Path("/tmp") or target.name != folder:
        raise ValueError("folder must be one directory name below /tmp")
    shutil.rmtree(target, ignore_errors=True)
