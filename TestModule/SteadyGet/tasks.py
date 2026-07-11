"""Build and run the independent SteadyGet Conan consumer."""
from pathlib import Path
import shlex
import shutil
from invoke import task

ROOT = Path(__file__).parent


def quote(value):
    return shlex.quote(str(value))


@task
def configure(context, build_type="Debug", remote="radxa-conan-pr"):
    """Install dependencies and generate the Conan CMake preset."""
    with context.cd(str(ROOT)):
        context.run(f"conan install . --build=missing -r={quote(remote)} -s build_type={quote(build_type)}")


@task
def build(context, build_type="Debug", remote="radxa-conan-pr"):
    """Configure when needed and build steady-get."""
    if not (ROOT / "CMakeUserPresets.json").exists():
        configure(context, build_type=build_type, remote=remote)
    preset = f"conan-{build_type.lower()}"
    with context.cd(str(ROOT)):
        context.run(f"cmake --preset {quote(preset)}")
        context.run(f"cmake --build --preset {quote(preset)}")


@task
def download(context, url, output=None, build_type="Debug", remote="radxa-conan-pr",
             max_size_mib=1024, timeout_seconds=300, ca_file=None):
    """Download URL with steady-get."""
    executable = ROOT / "build" / build_type / "steady-get"
    if not executable.exists():
        build(context, build_type=build_type, remote=remote)
    command = (f"{quote(executable)} {quote(url)} --max-size-mib={int(max_size_mib)} "
               f"--timeout-seconds={int(timeout_seconds)}")
    if output:
        command += f" --output {quote(Path(output).expanduser())}"
    if ca_file:
        command += f" --ca-file {quote(Path(ca_file).expanduser())}"
    context.run(command)


@task
def clean(_context):
    """Remove generated Conan and CMake files."""
    shutil.rmtree(ROOT / "build", ignore_errors=True)
    (ROOT / "CMakeUserPresets.json").unlink(missing_ok=True)
