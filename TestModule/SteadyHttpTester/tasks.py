"""Build and repeatedly run the independent Conan package tester."""
from pathlib import Path
import shlex
import shutil
import socket
from urllib.parse import urlsplit
from invoke import task
from invoke.exceptions import Exit

ROOT = Path(__file__).parent


def quote(value):
    return shlex.quote(str(value))


@task
def configure(context, build_type="Debug", remote="radxa-conan-pr"):
    """Install steady-http from Conan and generate the CMake preset."""
    with context.cd(str(ROOT)):
        context.run(f"conan install . --build=missing -r={quote(remote)} -s build_type={quote(build_type)}")


@task
def build(context, build_type="Debug", remote="radxa-conan-pr"):
    """Configure when needed and build the tester."""
    preset = f"conan-{build_type.lower()}"
    if not (ROOT / "CMakeUserPresets.json").exists():
        configure(context, build_type=build_type, remote=remote)
    with context.cd(str(ROOT)):
        context.run(f"cmake --preset {quote(preset)}")
        context.run(f"cmake --build --preset {quote(preset)}")


@task
def graph(context, build_type="Debug", remote="radxa-conan-pr", output="dependency-graph"):
    """Export the resolved Conan dependency graph as HTML, DOT, and JSON."""
    output_dir = ROOT / output
    output_dir.mkdir(parents=True, exist_ok=True)
    base = (f"conan graph info . -r={quote(remote)} "
            f"-s build_type={quote(build_type)}")
    with context.cd(str(ROOT)):
        for graph_format in ("html", "dot", "json"):
            destination = output_dir / f"steady-http-dependencies.{graph_format}"
            context.run(
                f"{base} --format={graph_format} --out-file={quote(destination)}"
            )
    print(f"dependency graphs written to {output_dir}")


@task
def test(context, file, upload_url, build_type="Debug", remote="radxa-conan-pr"):
    """Upload FILE to UPLOAD_URL, download it, and compare byte counts."""
    source = Path(file).expanduser().resolve()
    if not source.is_file():
        raise Exit(f"input file does not exist: {source}", code=2)
    parsed = urlsplit(upload_url)
    if parsed.scheme != "http" or not parsed.hostname or parsed.path in {"", "/"}:
        raise Exit("upload-url must look like http://host:port/file-name", code=2)
    try:
        with socket.create_connection((parsed.hostname, parsed.port or 80), timeout=1):
            pass
    except OSError as error:
        raise Exit(f"server is not reachable: {parsed.hostname}:{parsed.port or 80}", code=2) from error
    executable = ROOT / "build" / build_type / "steady-http-tester"
    if not executable.exists():
        build(context, build_type=build_type, remote=remote)
    context.run(f"{quote(executable)} {quote(source)} {quote(upload_url)}")


@task
def clean(_context):
    """Remove generated Conan and CMake files."""
    shutil.rmtree(ROOT / "build", ignore_errors=True)
    (ROOT / "CMakeUserPresets.json").unlink(missing_ok=True)
    shutil.rmtree(ROOT / "dependency-graph", ignore_errors=True)
