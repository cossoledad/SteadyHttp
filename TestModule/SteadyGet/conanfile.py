from conan import ConanFile
from conan.tools.cmake import cmake_layout


class SteadyGetConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("steady-http/0.1.0")

    def layout(self):
        cmake_layout(self)
