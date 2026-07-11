from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class SteadyHttpConan(ConanFile):
    required_conan_version = ">=2.0"
    name = "steady-http"
    version = "0.1.0"
    package_type = "library"
    license = "MIT"
    description = "A maintainable C++20 HTTP/HTTPS binary transfer client."
    topics = ("http", "https", "boost", "beast", "asio")
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    exports_sources = "CMakeLists.txt", "cmake/*", "include/*", "src/*", "examples/*", "LICENSE", "README.md"

    def requirements(self):
        self.requires("boost/1.88.0")
        self.requires("openssl/3.5.4")

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self)
        toolchain.variables["STEADY_HTTP_BUILD_EXAMPLES"] = False
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        CMake(self).install()

    def package_info(self):
        self.cpp_info.libs = ["SteadyHttp"]
        self.cpp_info.set_property("cmake_file_name", "SteadyHttp")
        self.cpp_info.set_property("cmake_target_name", "SteadyHttp::SteadyHttp")
        self.cpp_info.bindirs = []
