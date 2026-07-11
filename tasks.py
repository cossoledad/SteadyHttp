"""Project tasks. Run `invoke --list` for the complete command list."""
from pathlib import Path
import shutil
from invoke import task

ROOT = Path(__file__).parent


def _run(context, command):
    """Execute a project-relative command with Invoke's directory context."""
    with context.cd(str(ROOT)):
        return context.run(command)


def _build_dir(build_type):
    return ROOT / "build" / build_type.lower()


def _toolchain_file(build_type):
    return _build_dir(build_type) / "build" / build_type / "generators" / "conan_toolchain.cmake"

def _configure(context, build_type):
    directory = _build_dir(build_type)
    directory.mkdir(parents=True, exist_ok=True)
    _run(context, f"conan install . --output-folder={directory} --build=missing -s build_type={build_type}")
    _run(context, f"cmake --fresh -S . -B {directory} -G Ninja -DCMAKE_TOOLCHAIN_FILE={_toolchain_file(build_type)} -DCMAKE_BUILD_TYPE={build_type} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DSTEADY_HTTP_BUILD_EXAMPLES=ON")
    return directory

def _ensure_configured(context, build_type):
    directory = _build_dir(build_type)
    if not (directory / "CMakeCache.txt").exists() or not _toolchain_file(build_type).exists():
        return _configure(context, build_type)
    return directory

@task
def configure(context, build_type="Debug"):
    """Install dependencies and configure a Ninja build directory."""
    _configure(context, build_type)

@task
def build(context, build_type="Debug", target=None, jobs=None):
    """Incrementally build the project."""
    directory = _ensure_configured(context, build_type)
    command = f"cmake --build {directory}"
    if target:
        command += f" --target {target}"
    if jobs:
        command += f" --parallel {jobs}"
    _run(context, command)

@task
def clean(context, all=False):
    """Remove build outputs; pass --all to remove every build directory."""
    target = ROOT / "build" if all else _build_dir("Debug")
    if target.exists():
        shutil.rmtree(target)

@task
def format(context):
    """Format C++ sources with clang-format."""
    files = [*ROOT.glob("include/**/*.hpp"), *ROOT.glob("src/**/*.cpp"), *ROOT.glob("tests/**/*.cpp"), *ROOT.glob("examples/**/*.cpp")]
    if files:
        _run(context, "clang-format -i " + " ".join(str(path) for path in files))

@task
def lint(context, build_type="Debug"):
    """Run clang-tidy using the generated compilation database."""
    directory = _ensure_configured(context, build_type)
    if shutil.which("clang-tidy") is None:
        raise RuntimeError("clang-tidy is not installed or not on PATH")
    _run(context, f"clang-tidy -p {directory} src/client.cpp src/error.cpp src/url.cpp")

@task
def package(context, build_type="Release"):
    """Install into a local staging directory and show its contents."""
    directory = _ensure_configured(context, build_type)
    _run(context, f"cmake --build {directory}")
    staging = directory / "stage"
    _run(context, f"cmake --install {directory} --prefix {staging}")
    _run(context, f"find {staging} -maxdepth 4 -type f | sort")

@task
def export(context, build_type="Release"):
    """Create the Conan package (the build type is passed to Conan)."""
    _run(context, f"conan create . --build=missing -s build_type={build_type}")

@task
def publish(context, remote="radxa-conan-pr", build_type="Release"):
    """Create and upload steady-http/0.1.0 to the selected Conan remote."""
    export(context, build_type=build_type)
    _run(context, f"conan upload steady-http/0.1.0 -r={remote} --confirm")

@task
def example(context, build_type="Debug"):
    """Build examples (examples are introduced in later phases)."""
    build(context, build_type=build_type, target="all")

@task
def all(context, build_type="Debug"):
    """Configure, build, and stage an install."""
    configure(context, build_type=build_type)
    build(context, build_type=build_type)
    package(context, build_type=build_type)
