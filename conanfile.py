from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class PlatformLoggingConan(ConanFile):
    name = "platform_logging"
    version = "0.1.0"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {
        "shared": True,
        "fPIC": True,
    }

    requires = (
        "nlohmann_json/3.11.3",
        "spdlog/1.13.0",
    )

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def validate(self):
        check_min_cppstd(self, "20")

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()

        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.cache_variables["PLATFORM_LOGGING_BUILD_SHARED"] = "ON" if bool(self.options.shared) else "OFF"
        toolchain.cache_variables["CMAKE_CXX_STANDARD"] = "20"
        toolchain.cache_variables["CMAKE_CXX_STANDARD_REQUIRED"] = "ON"
        toolchain.cache_variables["CMAKE_CXX_EXTENSIONS"] = "OFF"
        toolchain.generate()
