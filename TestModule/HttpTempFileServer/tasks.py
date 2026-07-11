"""Commands for the standalone temporary file server."""
from pathlib import Path
import shutil
from invoke import task


@task
def start(context, folder="steady-http-files", host="127.0.0.1", port=18080,
          chunk_delay_ms=0):
    """Start the server; uploaded files are stored under /tmp/FOLDER."""
    context.run(
        f"python3 {Path(__file__).parent / 'server.py'} "
        f"--folder={folder} --host={host} --port={int(port)} "
        f"--chunk-delay-ms={float(chunk_delay_ms)}"
    )


@task
def clean(_context, folder="steady-http-files"):
    """Delete the selected temporary storage folder."""
    target = Path("/tmp") / folder
    if target.parent != Path("/tmp") or target.name != folder:
        raise ValueError("folder must be one directory name below /tmp")
    shutil.rmtree(target, ignore_errors=True)
